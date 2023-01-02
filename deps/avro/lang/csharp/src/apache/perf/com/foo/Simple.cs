// ------------------------------------------------------------------------------
// <auto-generated>
//    Generated by avrogen.exe, version 0.9.0.0
//    Changes to this file may cause incorrect behavior and will be lost if code
//    is regenerated
// </auto-generated>
// ------------------------------------------------------------------------------
namespace com.foo
{
	using System;
	using System.Collections.Generic;
	using System.Text;
	using Avro;
	using Avro.Specific;
	
	public partial class Simple : ISpecificRecord
	{
		public static Schema _SCHEMA = Avro.Schema.Parse(@"{""type"":""record"",""name"":""Simple"",""namespace"":""com.foo"",""fields"":[{""name"":""myInt"",""type"":""int""},{""name"":""myLong"",""type"":""long""},{""name"":""myBool"",""type"":""boolean""},{""name"":""myDouble"",""type"":""double""},{""name"":""myFloat"",""type"":""float""},{""name"":""myBytes"",""type"":""bytes""},{""name"":""myString"",""type"":""string""},{""name"":""myNull"",""type"":""null""}]}");
		private int _myInt;
		private long _myLong;
		private bool _myBool;
		private double _myDouble;
		private float _myFloat;
		private byte[] _myBytes;
		private string _myString;
		private object _myNull;
		public virtual Schema Schema
		{
			get
			{
				return Simple._SCHEMA;
			}
		}
		public int myInt
		{
			get
			{
				return this._myInt;
			}
			set
			{
				this._myInt = value;
			}
		}
		public long myLong
		{
			get
			{
				return this._myLong;
			}
			set
			{
				this._myLong = value;
			}
		}
		public bool myBool
		{
			get
			{
				return this._myBool;
			}
			set
			{
				this._myBool = value;
			}
		}
		public double myDouble
		{
			get
			{
				return this._myDouble;
			}
			set
			{
				this._myDouble = value;
			}
		}
		public float myFloat
		{
			get
			{
				return this._myFloat;
			}
			set
			{
				this._myFloat = value;
			}
		}
		public byte[] myBytes
		{
			get
			{
				return this._myBytes;
			}
			set
			{
				this._myBytes = value;
			}
		}
		public string myString
		{
			get
			{
				return this._myString;
			}
			set
			{
				this._myString = value;
			}
		}
		public object myNull
		{
			get
			{
				return this._myNull;
			}
			set
			{
				this._myNull = value;
			}
		}
		public virtual object Get(int fieldPos)
		{
			switch (fieldPos)
			{
			case 0: return this.myInt;
			case 1: return this.myLong;
			case 2: return this.myBool;
			case 3: return this.myDouble;
			case 4: return this.myFloat;
			case 5: return this.myBytes;
			case 6: return this.myString;
			case 7: return this.myNull;
			default: throw new AvroRuntimeException("Bad index " + fieldPos + " in Get()");
			};
		}
		public virtual void Put(int fieldPos, object fieldValue)
		{
			switch (fieldPos)
			{
			case 0: this.myInt = (System.Int32)fieldValue; break;
			case 1: this.myLong = (System.Int64)fieldValue; break;
			case 2: this.myBool = (System.Boolean)fieldValue; break;
			case 3: this.myDouble = (System.Double)fieldValue; break;
			case 4: this.myFloat = (System.Single)fieldValue; break;
			case 5: this.myBytes = (System.Byte[])fieldValue; break;
			case 6: this.myString = (System.String)fieldValue; break;
			case 7: this.myNull = (System.Object)fieldValue; break;
			default: throw new AvroRuntimeException("Bad index " + fieldPos + " in Put()");
			};
		}
	}
}
