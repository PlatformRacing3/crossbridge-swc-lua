/*
 * =BEGIN MIT LICENSE
 * 
 * The MIT License (MIT)
 *
 * Copyright (c) 2014 The CrossBridge Team
 * https://github.com/crossbridge-community
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 * 
 * =END MIT LICENSE
 *
 */
package {
	import crossbridge.lua.CModule;
	import crossbridge.lua.vfs.ISpecialFile;
	import crossbridge.lua.__lua_objrefs;
	import crossbridge.lua.LuaState;
	import crossbridge.lua.LuaReference;
	import crossbridge.lua.LuaTextField;

	import flash.display.SimpleButton;
	import flash.display.Sprite;
	import flash.events.Event;
	import flash.text.Font;
	import flash.text.TextField;
	import flash.text.TextFieldType;
	import flash.text.TextFormat;
	import flash.utils.getTimer;
	import flash.utils.ByteArray;

[SWF(width="800", height="600", backgroundColor="#999999", frameRate="60")]
	public class Main extends Sprite implements ISpecialFile {
	

		internal var luastate:LuaState;

		private var inbox:LuaTextField;

		private var outbox:TextField;
		
		private var outArray:Array;

		private var runtimelabel:TextField;

		public function Main() {
			addEventListener(Event.ADDED_TO_STAGE, appInit);
		}

		internal function appInit(event:Event):void {
			removeEventListener(Event.ADDED_TO_STAGE, appInit);

			runtimelabel = getTextField(5, 5, 790, 20);
			inbox = getLuaTextField(5, 30, 790, 275);
			outbox = getTextField(5, 310, 790, 275);
			outArray = new Array();

			inbox.text = "-- paste your LUA code here ...";
			inbox.type = TextFieldType.INPUT;
			inbox.addEventListener(Event.CHANGE, runScript);

			CModule.rootSprite = this;
			CModule.vfs.console = this;
			CModule.startAsync(this);

			runScript();
		}

		private function getTextField(x:int, y:int, w:int, h:int):TextField {
			var result:TextField = new TextField();
			result.width = w;
			result.height = h;
			result.x = x;
			result.y = y;
			result.multiline = true;
			result.selectable = true;
			result.wordWrap = true;
			result.background = true;
			result.backgroundColor = 0xFFFFFF;
			addChild(result);
			const tf:TextFormat = new TextFormat("Courier New", 12, 0x000000);
			result.defaultTextFormat = tf;
			return result;
		}
		
		private function getLuaTextField(x:int, y:int, w:int, h:int):LuaTextField {
			var result:LuaTextField = new LuaTextField();
			result.width = w;
			result.height = h;
			result.x = x;
			result.y = y;
			result.multiline = true;
			result.selectable = true;
			//result.wordWrap = true;
			result.background = true;
			result.backgroundColor = 0xFFFFFF;
			addChild(result);
			return result;
		}
	
		private static function getOutputFromArray(arr:Array):String {
			var strArr:Array = new Array();
			var len:int = 0;
			for (var i:int = 0; i < arr.length; i++) {
				var str:String = null;
				if (arr[i] == null) {
					str = "nil";
				} else if (arr[i] is String) {
					str = "\"" + arr[i] + "\"";
				} else {
					str = arr[i].toString();
				}
				if (i+1 == arr.length) {
					strArr[i] = str
				} else {
					strArr[i] = str + ", "
				}
				len += strArr[i].length;
			}
			var buff:ByteArray = new ByteArray();
			buff.length = len;
			for (i = 0; i < strArr.length; i++){
				buff.writeUTFBytes(strArr[i]);
			}
			buff.position = 0;
			return buff.readUTFBytes(len);
		}

		internal function runScript(event:Event = null):void {
			outbox.text = "";
			luastate = new LuaState();
			luastate.setGlobal("AS3Test", Test);

			var arr:Array;
			try{
				arr = luastate.loadString(inbox.text);
			} catch(e:Error) {
				output(e.toString());
				pushOutput();
				luastate.close();
				return;
			}
			
			if (arr[0] != 0) {
				output("Failed to parse script: " + arr[1]);
				pushOutput();
				luastate.close();
				return;
			}
			

			try{
				var runtime:int = getTimer();
				arr = (arr[1] as LuaReference).call();
				runtime = getTimer() - runtime;
				runtimelabel.text = "Script time: " + runtime + "ms";

				if (arr[0] != 0) {
					output("Failed to run script: " + arr[1]);
				} else {
					arr.shift();
					if (arr.length > 1) {
							output("Script returned " + arr.length + " values: " + getOutputFromArray(arr) + ".");
						} else if (arr.length == 1) {
							output("Script returned " + getOutputFromArray(arr) + ".");
						} else {
							output("Script finished.");
						}
				}
			} catch(e:Error) {
				output("Script threw AS3 error!\n" + e.toString() + "\n" + e.getStackTrace());
			}
			
			pushOutput();
			
			luastate.close();
		}
	
		private function pushOutput() : void {
			outbox.text += outArray.join("");
			outArray.length = 0;
		}

		public function output(s:String):void {
			//outbox.text += s;
			outArray.push(s);
			//trace(s);
		}

		/**
		 * The PlayerKernel implementation will use this function to handle
		 * C IO write requests to the file "/dev/tty" (e.g. output from
		 * printf will pass through this function). See the ISpecialFile
		 * documentation for more information about the arguments and return value.
		 */
		public function write(fd:int, bufPtr:int, nbyte:int, errnoPtr:int):int {
			var str:String = CModule.readString(bufPtr, nbyte)
			output(str)
			return nbyte
		}

		/**
		 * The PlayerKernel implementation will use this function to handle
		 * C IO read requests to the file "/dev/tty" (e.g. reads from stdin
		 * will expect this function to provide the data). See the ISpecialFile
		 * documentation for more information about the arguments and return value.
		 */
		public function read(fd:int, bufPtr:int, nbyte:int, errnoPtr:int):int {
			return 0
		}

		/**
		 * The PlayerKernel implementation will use this function to handle
		 * C fcntl requests to the file "/dev/tty"
		 * See the ISpecialFile documentation for more information about the
		 * arguments and return value.
		 */
		public function fcntl(fd:int, com:int, data:int, errnoPtr:int):int {
			return 0
		}

		/**
		 * The PlayerKernel implementation will use this function to handle
		 * C ioctl requests to the file "/dev/tty"
		 * See the ISpecialFile documentation for more information about the
		 * arguments and return value.
		 */
		public function ioctl(fd:int, com:int, data:int, errnoPtr:int):int {
			return 0;
		}
	}
}
