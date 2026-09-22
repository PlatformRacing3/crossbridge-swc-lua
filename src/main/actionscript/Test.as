package{
	import crossbridge.lua.CModule;
	import crossbridge.lua.LuaState;
	import crossbridge.lua.LuaReference;
	import crossbridge.lua.LuaResultArray;
	import flash.media.Sound;
	import flash.utils.ByteArray;

	public class Test {

		public static function getTable() : LuaReference
		{
			var tab:LuaReference = CModule.rootSprite.luastate.newTable();
			for (var i:int = 0; i < 5; i++) {
				tab.setField(i+1, i*i);
			}
			tab.setField("value", 51);
			tab.setField(true, "Freaky!");
			return tab;
		}

		public static function preAllocGetTable() : LuaReference
		{
			var tab:LuaReference = CModule.rootSprite.luastate.newTable(5, 2);
			for (var i:int = 0; i < 5; i++) {
				tab.setField(i+1, i*i);
			}
			tab.setField("value", 51);
			tab.setField(true, "Freaky!");
			return tab;
		}

		public static function setTableField(l : LuaReference, k : Object, v : Object) : void
		{
			l.setField(k, v);
		}

		public static function execute(f : LuaReference) : void
		{
			f.execute();
		}

		public static function callWith(f: LuaReference, ... args) : Array
		{
			return f.call.apply(f,args);
		}

		public static function flipBytes(bytes : ByteArray) : ByteArray
		{
			var newBytes:ByteArray = new ByteArray();
			newBytes.length = bytes.length;
			var i : int = bytes.length - 1;
			while (i >= 0) {
				bytes.position = i;
				newBytes.writeByte(bytes.readByte());
				i--;
			}
			return newBytes;
		}
		
		public static function getValues() : LuaResultArray
		{
			var arr:LuaResultArray = new LuaResultArray();
			arr[0] = 1; 
			arr[1] = 2; 
			arr[2] = 5; 
			arr[3] = "Awesome!"; 
			arr[4] = 3;
			return arr;
		}
		
		public static function playBytesAsSound(byteArr : ByteArray) : void
		{
			byteArr.position = 0;
			var sound:Sound = new Sound();
			sound.loadPCMFromByteArray(byteArr, uint(byteArr.length/4), "float", false, 44100.0);
			sound.play();
		}
		
	}
}