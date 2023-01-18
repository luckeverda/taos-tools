/*
 * Copyright (c) 2019 TAOS Data, Inc. <jhtao@taosdata.com>
 *
 * This program is free software: you can use, redistribute, and/or modify
 * it under the terms of the MIT license as published by the Free Software
 * Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.
 */

#include "bench.h"
#include "benchData.h"
#include "benchInsertMix.h"

//
// ------------------ mix ratio area -----------------------
//

#define MDIS 0
#define MUPD 1
#define MDEL 2

#define MCNT 3

#define  NEED_TAKEOUT_ROW_TOBUF(type)  (mix->genCnt[type] > 0 && mix->doneCnt[type] + mix->bufCnt[type] < mix->genCnt[type] && taosRandom()%100 <= mix->ratio[type]*2)
#define  FORCE_TAKEOUT(type) (mix->insertedRows * 100 / mix->insertRows > 80)
#define  RD(max) (taosRandom() % max)


typedef struct {
  uint64_t insertRows;   // need insert 
  uint64_t insertedRows; // already inserted

  int8_t ratio[MCNT];
  int64_t range[MCNT];

  // need generate count , calc from stb->insertRows * ratio
  uint64_t genCnt[MCNT];

  // status aleady done count
  uint64_t doneCnt[MCNT];

  // task out from batch to list buffer
  TSKEY* buf[MCNT];

  // bufer cnt
  uint64_t capacity[MCNT]; // capacity size for buf
  uint64_t bufCnt[MCNT];   // current valid cnt in buf

  // calc need value
  int32_t curBatchCnt;

} SMixRatio;

typedef struct {
  uint64_t ordRows; // add new order rows
  uint64_t updRows;
  uint64_t disRows;
  uint64_t delRows;
} STotal;


void mixRatioInit(SMixRatio* mix, SSuperTable* stb) {
  memset(mix, 0, sizeof(SMixRatio));
  mix->insertRows = stb->insertRows;
  uint32_t batchSize = g_arguments->reqPerReq;

  if (batchSize == 0) batchSize = 1;

  // set ratio
  mix->ratio[MDIS] = stb->disRatio;
  mix->ratio[MUPD] = stb->updRatio;
  mix->ratio[MDEL] = stb->delRatio;

  // set range
  mix->range[MDIS] = stb->disRange;
  mix->range[MUPD] = stb->updRange;
  mix->range[MDEL] = stb->delRange;

  // calc count
  mix->genCnt[MDIS] = mix->insertRows * stb->disRatio / 100;
  mix->genCnt[MUPD] = mix->insertRows * stb->updRatio / 100;
  mix->genCnt[MDEL] = mix->insertRows * stb->delRatio / 100;

  // malloc buffer
  for (int32_t i = 0; i < MCNT - 1; i++) {
    // max
    if (mix->genCnt[i] > 0) {
      // buffer max count calc
      mix->capacity[i] = batchSize * 3 + mix->genCnt[i]*5 / 1000;
      mix->buf[i] = calloc(mix->capacity[i], sizeof(TSKEY));
    } else {
      mix->capacity[i] = 0;
      mix->buf[i] = NULL;
    }
    mix->bufCnt[i] = 0;
  }
}

void mixRatioExit(SMixRatio* mix) {
  // free buffer
  for (int32_t i = 0; i < MCNT; i++) {
    if (mix->buf[i]) {
      free(mix->buf[i]);
      mix->buf[i] = NULL;
    }
  }
}

//
//  --------------------- util ----------------
//

// return true can do execute delelte sql
bool needExecDel(SMixRatio* mix) {
  if (mix->genCnt[MDEL] == 0 || mix->doneCnt[MDEL] >= mix->genCnt[MDEL]) {
    return false;
  }

  return true;
}

//
// ------------------ gen area -----------------------
//

//
// generate head
//
uint32_t genInsertPreSql(threadInfo* info, SDataBase* db, SSuperTable* stb, char* tableName, uint64_t tableSeq, char* pstr) {
  uint32_t len = 0;
  // ttl
  char ttl[20] = "";
  if (stb->ttl != 0) {
    sprintf(ttl, "TTL %d", stb->ttl);
  }

  if (stb->partialColNum == stb->cols->size) {
    if (stb->autoCreateTable) {
      len = snprintf(pstr, MAX_SQL_LEN, "%s %s.%s USING %s.%s TAGS (%s) %s VALUES ", STR_INSERT_INTO, db->dbName,
                     tableName, db->dbName, stb->stbName, stb->tagDataBuf + stb->lenOfTags * tableSeq, ttl);
    } else {
      len = snprintf(pstr, MAX_SQL_LEN, "%s %s.%s VALUES ", STR_INSERT_INTO, db->dbName, tableName);
    }
  } else {
    if (stb->autoCreateTable) {
      len = snprintf(pstr, MAX_SQL_LEN, "%s %s.%s (%s) USING %s.%s TAGS (%s) %s VALUES ", STR_INSERT_INTO,
                     db->dbName, tableName, stb->partialColNameBuf, db->dbName, stb->stbName,
                     stb->tagDataBuf + stb->lenOfTags * tableSeq, ttl);
    } else {
      len = snprintf(pstr, MAX_SQL_LEN, "%s %s.%s (%s) VALUES ", STR_INSERT_INTO, db->dbName, tableName,
                     stb->partialColNameBuf);
    }
  }

  return len;
}

//
// generate delete pre sql like "delete from st"
//
uint32_t genDelPreSql(SDataBase* db, SSuperTable* stb, char* tableName, char* pstr) {
  uint32_t len = 0;
  // super table name or child table name random select
  char* name = RD(2) ? tableName : stb->stbName;
  len = snprintf(pstr, MAX_SQL_LEN, "delete from %s.%s where ", db->dbName, name);

  return len;
}

//
// append row to batch buffer
//
uint32_t appendRowRuleOld(SSuperTable* stb, char* pstr, uint32_t len, int64_t timestamp) {
  uint32_t size = 0;
  int32_t  pos = RD(g_arguments->prepared_rand);
  int      disorderRange = stb->disorderRange;

  if (stb->useSampleTs && !stb->random_data_source) {
    size = snprintf(pstr + len, MAX_SQL_LEN - len, "(%s)", stb->sampleDataBuf + pos * stb->lenOfCols);
  } else {
    int64_t disorderTs = 0;
    if (stb->disorderRatio > 0) {
      int rand_num = taosRandom() % 100;
      if (rand_num < stb->disorderRatio) {
        disorderRange--;
        if (0 == disorderRange) {
          disorderRange = stb->disorderRange;
        }
        disorderTs = stb->startTimestamp - disorderRange;
        debugPrint(
            "rand_num: %d, < disorderRatio:"
            " %d, disorderTs: %" PRId64 "\n",
            rand_num, stb->disorderRatio, disorderTs);
      }
    }
    // generate
    size = snprintf(pstr + len, MAX_SQL_LEN - len, "(%" PRId64 ",%s)", disorderTs ? disorderTs : timestamp,
                    stb->sampleDataBuf + pos * stb->lenOfCols);
  }

  return size;
}



// create columns data 
uint32_t createColsDataRandom(SSuperTable* stb, char* pstr, uint32_t len, int64_t ts) {
  uint32_t size = 0;
  int32_t  pos = RD(g_arguments->prepared_rand);

  // gen row data
  size = snprintf(pstr + len, MAX_SQL_LEN - len, "(%" PRId64 ",%s)", ts, stb->sampleDataBuf + pos * stb->lenOfCols);
  
  return size;
}

// take out row
bool takeRowOutToBuf(SMixRatio* mix, uint8_t type, int64_t ts) {
    int64_t* buf = mix->buf[type];
    if(buf == NULL){
        return false;
    }

    if(mix->bufCnt[type] >= mix->capacity[type]) {
        // no space to save
        return false;
    }

    uint64_t bufCnt = mix->bufCnt[type];

    // save
    buf[bufCnt] = ts;
    // move next
    mix->bufCnt[type] += 1;

    return true;
}

//
// row rule mix , global info put into mix
//
#define MIN_COMMIT_ROWS 10000
uint32_t appendRowRuleMix(threadInfo* info, SSuperTable* stb, SMixRatio* mix, char* pstr, uint32_t len, int64_t ts, uint32_t* pGenRows) {
    uint32_t size = 0;
    
    // remain need generate rows
    bool forceDis = FORCE_TAKEOUT(MDIS);
    bool forceUpd = FORCE_TAKEOUT(MUPD);

    // disorder
    if ( forceDis || NEED_TAKEOUT_ROW_TOBUF(MDIS)) {
        // need take out current row to buf
        if(takeRowOutToBuf(mix, MDIS, ts)){
            return 0;
        }
    }

    // gen col data
    size = createColsDataRandom(stb, pstr, len + size, ts);
    if(size > 0) {
      *pGenRows += 1;
      debugPrint("    row ord ts=%" PRId64 " \n", ts);
    }

    // update
    if (forceUpd || NEED_TAKEOUT_ROW_TOBUF(MUPD)) {
        takeRowOutToBuf(mix, MUPD, ts);
    }

    return size;
}

//
// fill update rows from mix
//
uint32_t fillBatchWithBuf(SSuperTable* stb, SMixRatio* mix, int64_t startTime, char* pstr, uint32_t len, uint32_t* pGenRows, uint8_t type, uint32_t maxFill, bool force) {
    uint32_t size = 0;
    if (maxFill == 0) return 0;

    uint32_t rdFill = RD(maxFill*0.75);
    if(rdFill == 0) {
        rdFill = maxFill;
    }

    int64_t* buf = mix->buf[type];
    int32_t  bufCnt = mix->bufCnt[type];

    if (force) {
        rdFill = bufCnt > maxFill ? maxFill : bufCnt;
    } else {
        if (rdFill > bufCnt) {
            rdFill = bufCnt / 2;
        }
    }

    // fill from buf
    int32_t selCnt = 0;
    int32_t findCnt = 0;
    int64_t ts;
    int32_t i;
    int32_t multiple = force ? 4 : 2;

    while(selCnt < rdFill && bufCnt > 0 && ++findCnt < rdFill * multiple) {
        // get ts
        i = RD(bufCnt);
        ts = buf[i];
        if( ts >= startTime && force == false) {
            // in current batch , ignore
            continue;
        }

        // generate row by ts
        size += createColsDataRandom(stb, pstr, len + size, ts);
        *pGenRows += 1;
        selCnt ++;
        debugPrint("    row %s ts=%" PRId64 " \n", type == MDIS ? "dis" : "upd", ts);

        // remove current item
        mix->bufCnt[type] -= 1;
        buf[i] = buf[bufCnt - 1]; // last set to current
        bufCnt = mix->bufCnt[type]; 
    }

    return size;
}


//
// generate  insert batch body, return rows in batch
//
uint32_t genBatchSql(threadInfo* info, SSuperTable* stb, SMixRatio* mix, int64_t* pStartTime, char* pstr, uint32_t slen, STotal* pBatT) {
  int32_t genRows = 0;
  int64_t  ts = *pStartTime;
  int64_t  startTime = *pStartTime;
  uint32_t len = slen; // slen: start len
  uint32_t ordRows = 0;

  bool forceDis = FORCE_TAKEOUT(MDIS);
  bool forceUpd = FORCE_TAKEOUT(MUPD);

  debugPrint("  batch gen StartTime=%" PRId64 " batchID=%d \n", *pStartTime, mix->curBatchCnt);
  int32_t last = -1;

  while ( genRows < g_arguments->reqPerReq) {
    last = genRows;
    switch (stb->genRowRule) {
      case RULE_OLD:
        len += appendRowRuleOld(stb, pstr, len, ts);
        genRows ++;
        pBatT->ordRows ++;
        break;
      case RULE_MIX:
        // add new row (maybe del)
        if (mix->insertedRows + pBatT->disRows + pBatT->ordRows  < mix->insertRows) {
          ordRows = 0;
          len += appendRowRuleMix(info, stb, mix, pstr, len, ts, &ordRows);
          if (ordRows > 0) {
            genRows += ordRows;
            pBatT->ordRows += ordRows;
          } else {
            // takeout to disorder list, so continue to gen
            last = -1;
          }
        }

        if(genRows >= g_arguments->reqPerReq) {
          break;
        }

        if( forceUpd || RD(stb->fillIntervalUpd) == 0) {
            // fill update rows from buffer
            uint32_t maxFill = stb->fillIntervalUpd/2;
            if(maxFill > g_arguments->reqPerReq - genRows) {
              maxFill = g_arguments->reqPerReq - genRows;
            }
            // calc need count
            int32_t remain = mix->genCnt[MUPD] - mix->doneCnt[MUPD] - pBatT->updRows;
            if (remain > 0) {
              if (maxFill > remain) {
                maxFill = remain;
              }

              uint32_t updRows = 0;
              len += fillBatchWithBuf(stb, mix, startTime, pstr, len, &updRows, MUPD, maxFill, forceUpd);
              if (updRows > 0) {
                genRows += updRows;
                if (genRows >= g_arguments->reqPerReq) {
                  break;
                }
                pBatT->updRows += updRows;
              }
            }
        }

        if( forceDis || RD(stb->fillIntervalDis) == 0) {
            // fill disorder rows from buffer
            uint32_t maxFill = stb->fillIntervalDis;
            if(maxFill > g_arguments->reqPerReq - genRows) {
              maxFill = g_arguments->reqPerReq - genRows;
            }
            // calc need count
            int32_t remain = mix->genCnt[MDIS] - mix->doneCnt[MDIS] - pBatT->disRows;
            if (remain > 0) {
              if (maxFill > remain) {
                maxFill = remain;
              }

              uint32_t disRows = 0;
              len += fillBatchWithBuf(stb, mix, startTime, pstr, len, &disRows, MDIS, maxFill, forceDis);
              if (disRows > 0) {
                genRows += disRows;
                pBatT->disRows += disRows;
              }
            }
        }
        break;
      default:
        break;
    }

    // move next ts
    ts += stb->timestamp_step;

    // check over MAX_SQL_LENGTH
    if (len > (MAX_SQL_LEN - stb->lenOfCols)) {
      break;
    }

    if(genRows == last) {
      // now new row fill
      break;
    }
  }

  *pStartTime = ts;
  debugPrint("  batch gen EndTime=  %" PRId64 " genRows=%d \n", *pStartTime, genRows);

  return genRows;
}

//
// generate delete batch body
//
uint32_t genBatchDelSql(SSuperTable* stb, SMixRatio* mix, int64_t batStartTime, char* pstr, uint32_t slen) {
  uint32_t len = slen;  // slen: start len
  if (stb->genRowRule != RULE_MIX) {
    return 0;
  }

  int64_t range = batStartTime - stb->startTimestamp;
  int64_t rangeCnt = range / stb->timestamp_step;

  if (rangeCnt < 200) return 0;

  int32_t batCnt  = mix->insertRows / g_arguments->reqPerReq;
  int32_t eachCnt = mix->genCnt[MDEL] / batCnt;

  // get count
  uint32_t count = RD(eachCnt * 2);
  if (count > rangeCnt) {
    count = rangeCnt;
  }
  if (count == 0) return 0;

  int64_t ds = batStartTime - RD(range);
  int64_t de = ds + count * stb->timestamp_step;

  char where[128] = "";
  sprintf(where, " ts >= %" PRId64 " and ts < %" PRId64 ";", ds, de);

  len += snprintf(pstr + len, MAX_SQL_LEN - len, "%s", where);
  debugPrint("  batch delete range=%s \n", where);

  return count;
}

//
// insert data to db->stb with info
//
bool insertDataMix(threadInfo* info, SDataBase* db, SSuperTable* stb) {
  int64_t lastPrintTime = 0;

  // check interface
  if (stb->iface != TAOSC_IFACE) {
    return false;
  }

  // debug
  //g_arguments->debug_print = true;

  STotal total;
  memset(&total, 0, sizeof(STotal));

  // loop insert child tables
  for (uint64_t tbIdx = info->start_table_from; tbIdx <= info->end_table_to; ++tbIdx) {
    char* tbName = stb->childTblName[tbIdx];

    SMixRatio mixRatio;
    mixRatioInit(&mixRatio, stb);
    uint32_t len = 0;
    int64_t batStartTime = stb->startTimestamp;
    STotal tbTotal;
    memset(&tbTotal, 0 , sizeof(STotal));

    while (mixRatio.insertedRows < mixRatio.insertRows) {
      // check terminate
      if (g_arguments->terminate) {
        break;
      }

      // generate pre sql  like "insert into tbname ( part column names) values  "
      len = genInsertPreSql(info, db, stb, tbName, tbIdx, info->buffer);

      // batch create sql values
      STotal batTotal;
      memset(&batTotal, 0 , sizeof(STotal));
      uint32_t batchRows = genBatchSql(info, stb, &mixRatio, &batStartTime, info->buffer, len, &batTotal);

      // execute insert sql
      int64_t startTs = toolsGetTimestampUs();
      //g_arguments->debug_print = false;
      if (execBufSql(info, batchRows) != 0) {
        g_fail = true;
        g_arguments->terminate = true;
        break;
      }
      //g_arguments->debug_print = true;
      int64_t endTs = toolsGetTimestampUs();

      // exec sql ok , update bat->total to table->total
      if (batTotal.ordRows > 0) {
        tbTotal.ordRows += batTotal.ordRows;
      }

      if (batTotal.disRows > 0) {
        tbTotal.disRows += batTotal.disRows;
        mixRatio.doneCnt[MDIS] += batTotal.disRows;
      }

      if (batTotal.updRows > 0) {
        tbTotal.updRows += batTotal.updRows;
        mixRatio.doneCnt[MUPD] += batTotal.updRows;
      }

      // calc inserted rows = order rows + disorder rows
      mixRatio.insertedRows = tbTotal.ordRows + tbTotal.disRows;

      // delete
      if (needExecDel(&mixRatio)) {
        len = genDelPreSql(db, stb, tbName, info->buffer);
        batchRows = genBatchDelSql(stb, &mixRatio, batStartTime, info->buffer, len);
        if (batchRows > 0) {
            //g_arguments->debug_print = false;
            if (execBufSql(info, batchRows) != 0) {
              g_fail = true;
              break;
            }
            //g_arguments->debug_print = true;
            tbTotal.delRows += batchRows;
            mixRatio.doneCnt[MDEL] += batchRows;
        }
      }

      // sleep if need
      if (stb->insert_interval > 0) {
        debugPrint("%s() LN%d, insert_interval: %" PRIu64 "\n", __func__, __LINE__, stb->insert_interval);
        perfPrint("sleep %" PRIu64 " ms\n", stb->insert_interval);
        toolsMsleep((int32_t)stb->insert_interval);
      }

      // show
      int64_t delay = endTs - startTs;
      if (delay <= 0) {
        debugPrint("thread[%d]: startTS: %" PRId64 ", endTS: %" PRId64 "\n", info->threadID, startTs, endTs);
      } else {
        perfPrint("insert execution time is %10.2f ms\n", delay / 1E6);

        int64_t* pdelay = benchCalloc(1, sizeof(int64_t), false);
        *pdelay = delay;
        benchArrayPush(info->delayList, pdelay);
        info->totalDelay += delay;
      }
       
      int64_t currentPrintTime = toolsGetTimestampMs();
      if (currentPrintTime - lastPrintTime > 30 * 1000) {
        infoPrint("thread[%d] has currently inserted rows: %" PRIu64 "\n", info->threadID,
                  info->totalInsertRows);
        lastPrintTime = currentPrintTime;
      }
      

      // total
      mixRatio.curBatchCnt++;
    }  // row end

    // print
    infoPrint("table:%s inserted ok, inserted(%"  PRId64 ") rows order(%" PRId64 ")  disorder(%" PRId64 ") update(%" PRId64") delete(%" PRId64 ") \n",
              tbName, mixRatio.insertedRows, tbTotal.ordRows, tbTotal.disRows, tbTotal.updRows, tbTotal.delRows);

    // table total -> all total
    total.ordRows += tbTotal.ordRows;
    total.delRows += tbTotal.delRows;
    total.disRows += tbTotal.disRows;
    total.updRows += tbTotal.updRows;

    info->totalInsertRows +=mixRatio.insertedRows;
  }  // child table end


  // end
  if (0 == info->totalDelay) info->totalDelay = 1;


  // total
  info->totalInsertRows = total.ordRows + total.disRows;
  succPrint("thread[%d] %s(), completed total inserted rows: %" PRIu64 ", %.2f records/second\n", info->threadID,
            __func__, info->totalInsertRows, (double)(info->totalInsertRows / ((double)info->totalDelay / 1E6)));

  // print
  succPrint("inserted finished. \n    rows order: %" PRId64 " \n    disorder: %" PRId64 " \n    update: %" PRId64" \n    delete: %" PRId64 " \n",
            total.ordRows, total.disRows, total.updRows, total.delRows);

  //g_arguments->debug_print = false;
  return true;
}
