###################################################################
#           Copyright (c) 2016 by TAOS Technologies, Inc.
#                     All rights reserved.
#
#  This file is proprietary and confidential to TAOS Technologies.
#  No part of this file may be reproduced, stored, transmitted,
#  disclosed or used in any form or by any means other than as
#  expressly provided by the written permission from Jianhui Tao
#
###################################################################

# -*- coding: utf-8 -*-

import os
from util.log import *
from util.cases import *
from util.sql import *
from util.dnodes import *


class TDTestCase:
    def caseDescription(self):
        """
        case1<sdsang>: [TD-12526] taosdump supports bool
        """

    def init(self, conn, logSql):
        tdLog.debug("start to execute %s" % __file__)
        tdSql.init(conn.cursor(), logSql)
        self.tmpdir = "tmp"

    def getPath(self, tool="taosdump"):
        selfPath = os.path.dirname(os.path.realpath(__file__))

        if "community" in selfPath:
            projPath = selfPath[: selfPath.find("community")]
        elif "src" in selfPath:
            projPath = selfPath[: selfPath.find("src")]
        elif "/tools/" in selfPath:
            projPath = selfPath[: selfPath.find("/tools/")]
        elif "/tests/" in selfPath:
            projPath = selfPath[: selfPath.find("/tests/")]
        else:
            tdLog.info("Cannot find %s in path: %s" % (tool, selfPath))
            projPath = "/usr/local/taos/bin/"

        paths = []
        for root, dirs, files in os.walk(projPath):
            if (tool) in files:
                rootRealPath = os.path.dirname(os.path.realpath(root))
                if "packaging" not in rootRealPath:
                    paths.append(os.path.join(root, tool))
                    break
        if len(paths) == 0:
            return ""
        return paths[0]

    def run(self):
        tdSql.prepare()

        tdSql.execute("drop database if exists db")
        tdSql.execute("create database db  keep 3649 ")

        tdSql.execute("use db")
        tdSql.execute("create table st(ts timestamp, c1 BOOL) tags(btag BOOL)")
        tdSql.execute("create table t1 using st tags(true)")
        tdSql.execute("insert into t1 values(1640000000000, true)")
        tdSql.execute("create table t2 using st tags(false)")
        tdSql.execute("insert into t2 values(1640000000000, false)")
        tdSql.execute("create table t3 using st tags(NULL)")
        tdSql.execute("insert into t3 values(1640000000000, NULL)")

        binPath = self.getPath()
        if binPath == "":
            tdLog.exit("taosdump not found!")
        else:
            tdLog.info("taosdump found in %s" % binPath)

        if not os.path.exists(self.tmpdir):
            os.makedirs(self.tmpdir)
        else:
            print("directory exists")
            os.system("rm -rf %s" % self.tmpdir)
            os.makedirs(self.tmpdir)

        os.system("%s -R -D db -o %s" % (binPath, self.tmpdir))

        #        sys.exit(1)
        tdSql.execute("drop database db")

        os.system("%s -R -i %s" % (binPath, self.tmpdir))

        tdSql.query("show databases")
        dbresult = tdSql.queryResult

        found = False
        for i in range(len(dbresult)):
            print("Found db: %s" % dbresult[i][0])
            if dbresult[i][0] == "db":
                found = True
                break

        assert found == True

        tdSql.execute("use db")
        tdSql.query("show stables")
        tdSql.checkRows(1)
        tdSql.checkData(0, 0, "st")

        tdSql.query("show tables")
        tdSql.checkRows(3)
        dbresult = tdSql.queryResult
        print(dbresult)
        for i in range(len(dbresult)):
            assert (
                (dbresult[i][0] == "t1")
                or (dbresult[i][0] == "t2")
                or (dbresult[i][0] == "t3")
            )

        tdSql.query("select btag from st")
        tdSql.checkRows(3)
        dbresult = tdSql.queryResult
        print(dbresult)

        tdSql.query("select * from st where btag = true")
        tdSql.checkRows(1)
        tdSql.checkData(0, 1, "True")
        tdSql.checkData(0, 2, "True")

        tdSql.query("select * from st where btag = false")
        tdSql.checkRows(1)
        tdSql.checkData(0, 1, "False")
        tdSql.checkData(0, 2, "False")

        tdSql.query("select * from st where btag is null")
        dbresult = tdSql.queryResult
        print(dbresult)
        tdSql.checkRows(1)
        tdSql.checkData(0, 1, None)
        tdSql.checkData(0, 2, None)

    def stop(self):
        tdSql.close()
        tdLog.success("%s successfully executed" % __file__)


tdCases.addWindows(__file__, TDTestCase())
tdCases.addLinux(__file__, TDTestCase())
