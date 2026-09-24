package crossbridge.lua
{
	import avm2.intrinsics.memory.*;
	import crossbridge.lua.CModule;
	import flash.events.Event;
	import flash.events.TextEvent;
	import flash.geom.Rectangle;
	import flash.text.TextField;
	import flash.text.TextFormat;
	import flash.utils.Dictionary;
	import flash.utils.getTimer;
	
	/*
	algorithm:
		Two scan data vectors (we switch between them each scan)
		We find the first modified index:
			Preserve all scan data from before then exactly.
			Rescan modified range.
			Once past modified range, check for when it lines up again.
			Stop scanning when it lines up again, preserve all scan data after that. Adjust indices.
			Set the dirty range.
		On highlight:
			Unhighlight the dirty range.
			Highlight visible stuff.
	*/
	
	class LTF_FormatEntry
	{
		public var charStart:int;
		public var charEnd:int; // first character after the scan.
		public var formatType:int;
		public var highlighted:Boolean;
		
		public function LTF_FormatEntry(charStart:int, charEnd:int, formatType:int)
		{
			this.charStart = charStart;
			this.charEnd = charEnd;
			this.formatType = formatType;
			this.highlighted = false;
		}
		
		public function setData(charStart:int, charEnd:int, formatType:int) : void
		{
			this.charStart = charStart;
			this.charEnd = charEnd;
			this.formatType = formatType;
			this.highlighted = false;
		}
		
		// offset is how much later the same stuff should be on this.
		public function equals(entry:LTF_FormatEntry, offset:int = 0) : Boolean
		{
			return (
					this.charStart  == (entry.charStart + offset)
				&&  this.charEnd    == (entry.charEnd   + offset)
				&&  this.formatType == entry.formatType
			);
		}
		
		public function clone(offset:int = 0) : LTF_FormatEntry
		{
			var newEntry:LTF_FormatEntry = new LTF_FormatEntry(this.charStart + offset, this.charEnd + offset, this.formatType);
			newEntry.highlighted = this.highlighted;
			return newEntry;
		}
		
		public function copyFrom(entry:LTF_FormatEntry, offset:int = 0) : void
		{
			this.charStart = entry.charStart + offset;
			this.charEnd = entry.charEnd + offset;
			this.formatType = entry.formatType;
			this.highlighted = entry.highlighted;
		}
		
		public function toString() : String
		{
			return "{charStart: " + this.charStart + ", charEnd: " + this.charEnd + ", formatType: " + this.formatType + ", highlighted: " + this.highlighted + "}";
		}
	}
	
	public class LuaTextField extends TextField
	{
	
		// Character codes
		private static const CHAR_EOF:int = 0;
		private static const CHAR_NEWLINE:int = 13;
		private static const CHAR_QUOTATION:int = 34;
		private static const CHAR_APOSTROPHE:int = 39;
		private static const CHAR_PLUS:int = 43;
		private static const CHAR_DASH:int = 45;
		private static const CHAR_DOT:int = 46;
		private static const CHAR_EQUAL:int = 61;
		private static const CHAR_LBRACKET:int = 91;
		private static const CHAR_RBRACKET:int = 93;
		private static const CHAR_BACKSLASH:int = 92;
		private static const CHAR_ZERO:int = 48;
		private static const CHAR_UPPER_E:int = 69;
		private static const CHAR_LOWER_E:int = 101;
		private static const CHAR_UPPER_P:int = 80;
		private static const CHAR_LOWER_P:int = 112;
		private static const CHAR_UPPER_X:int = 88;
		private static const CHAR_LOWER_X:int = 120;
		
		// Character 'types'
		private static const CHARTYPE_OTHER:int = 0; // characters that aren't parts of lua's syntax at all? idk
		private static const CHARTYPE_SEPARATOR:int = 1;
		private static const CHARTYPE_IDSTART:int = 2; // these and below (higher numbers) can be part of identifiers.
		private static const CHARTYPE_NUMSTART:int = 3;
		
		// Highlighting types.
		private static const FMT_BASE:int = 0;
		private static const FMT_STRING_LITERAL:int = 1;
		private static const FMT_NUMBER_LITERAL:int = 2;
		private static const FMT_KEYWORD:int = 3;
		private static const FMT_LIBRARY_WORD:int = 4;
		private static const FMT_COMMENT:int = 5;
		
		private static const NUMCLASS_DECIMAL:int = 2; // Digits that are valid for decimal or hex literals.
		private static const NUMCLASS_HEX:int = 1; // Digits that are valid for hex.
		
		private static const IDENTIFIER_KEYWORD:int = 1;
		private static const IDENTIFIER_LIBWORD:int = 2;
		
		private static const charSeparator:Vector.<int> = new Vector.<int>(256,true);
		private static const charType:Vector.<int> = new Vector.<int>(256,true);
		private static const numeralClass:Vector.<int> = new Vector.<int>(256, true);
		private static const negativeHighlight:Vector.<int> = new Vector.<int>(256, true); // 2 indicates a minus is unary, 1 indicates to skip.
		private static const separator:String = " \n\t+-=[]/<>,.~#%^*(){};:'\""; // '-' needs a special exception. So do '.' and '['
		private static const idStart:String = "_abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"; // valid starter for highlightable identifier.
		private static const numStart:String = "0123456789"; // '-' needs a special thing here too. usually it's a separator though.
		private static const hexNum:String = "abcdefABCDEF"; // valid "digits" of hex literals.
		private static const negativeHighlightChars:String = "+-*/%^={([,<>";
		private static const negativeHighlightSkip:String = " \n\r\t";
				
		private static const colorDefault:Object = {
			"base"   : 0x000000,
			"string" : 0x027240,//0x006634,
			"number" : 0x6C21C6,//0x953A05,
			"keyword": 0x002DD6,//0xA8232D,
			"libword": 0xBE1A61,//0x9A277D,
			"comment": 0x6A6F81//0x686868
		};
		
		private static const IdentifierDictionary:Dictionary = new Dictionary();
		private static const keyWordList:Vector.<String> = new <String>["break", "goto", "do", "end", "while", "repeat", "until", "if", "then", "elseif", "else",
			"for", "in", "function", "local", "return", "and", "or", "not"
		];
		
		private static const literalWordList:Vector.<String> = new <String>["nil", "false", "true"];
		
		private static const libWordList:Vector.<String> = new <String>["_G", "assert", "bit32", "buffer", "coroutine", "dofile", "debug", "error", "flash", "getmetatable",
			"io", "ipairs", "load", "loadfile", "loadstring", "math", "module", "next", "os", "package", "pairs", "pcall", "print", "random", "rawequal", "rawget", "rawset",
			"require", "select", "setmetatable", "string", "table", "tonumber", "tostring", "type", "unpack", "xpcall",
			"arshift", "band", "bnot", "bor", "btest", "bxor", "extract", "lrotate", "lshift", "replace", "rrotate", "rshift",
			"copy", "fill", "fromstring", "len", "new", "readbits", "readf32", "readf64", "readi16", "readi32", "readi8", "readstring", "readu16", "readu32", "readu8",
			  "writebits", "writef32", "writef64", "writei16", "writei32", "writei8", "writestring", "writeu16", "writeu32", "writeu8",
			"create", "resume", "running", "status", "wrap", "yield",
			"debug", "gethook", "getinfo", "getlocal", "getmetatable", "getregistry", "getupvalue", "getuservalue", "sethook", "setlocal",
			  "setmetatable", "setupvalue", "setuservalue", "traceback", "upvalueid", "upvaluejoin",
			"abs", "acos", "asin", "atan", "atan2", "ceil", "clamp", "cos", "cosh", "deg", "exp", "floor", "fmod", "frexp", "huge", "isnan", "ldexp", "lerp", "log",
			  "log10", "max", "min", "modf", "nan", "pi", "pow", "rad", "random", "randomseed", "round", "sign", "sin", "sinh", "sqrt", "tan", "tanh", 
			"getclass", "gettimer", "registerConversion", "toarray", "toobject", "trace",
			"clock", "date", "difftime", "execute", "exit", "getenv", "remove", "rename", "setlocale", "time", "tmpname",
			"swap",
			"byte", "char", "dump", "find", "format", "gmatch", "gsub", "len", "lower", "match", "rep", "reverse", "sub", "upper", 
			"concat", "create", "find", "insert", "maxn", "pack", "remove", "sort", "unpack", 
		];
		
		
		{
			var i:int = 0;
			for (i = 0; i < separator.length; i++) {
				charSeparator[separator.charCodeAt(i)] = 1;
				charType[separator.charCodeAt(i)] = CHARTYPE_SEPARATOR;
			}
			charType[CHAR_NEWLINE] = CHARTYPE_SEPARATOR; // For some reason I can't define char(13) in a string but that's what you get when text is entered by a user???
			for (i = 0; i < idStart.length; i++) {
				charType[idStart.charCodeAt(i)] = CHARTYPE_IDSTART;
			}
			for (i = 0; i < numStart.length; i++) {
				charType[numStart.charCodeAt(i)] = CHARTYPE_NUMSTART;
				numeralClass[numStart.charCodeAt(i)] = NUMCLASS_DECIMAL;
			}
			for (i = 0; i < hexNum.length; i++) {
				numeralClass[hexNum.charCodeAt(i)] = NUMCLASS_HEX;
			}
			for (i = 0; i < negativeHighlightChars.length; i++) {
				negativeHighlight[negativeHighlightChars.charCodeAt(i)] = 2;
			}
			for (i = 0; i < negativeHighlightSkip.length; i++) {
				negativeHighlight[negativeHighlightSkip.charCodeAt(i)] = 1;
			}
			for (i = 0; i < keyWordList.length; i++) {
				IdentifierDictionary[keyWordList[i]] = FMT_KEYWORD;
			}
			for (i = 0; i < literalWordList.length; i++) {
				IdentifierDictionary[literalWordList[i]] = FMT_NUMBER_LITERAL; // steal number literal color for these.
			}
			for (i = 0; i < libWordList.length; i++) {
				IdentifierDictionary[libWordList[i]] = FMT_LIBRARY_WORD;
			}
		}
		
		private var formatList:Vector.<TextFormat> = new Vector.<TextFormat>(6, true);
		private var _highlightingEnabled:Boolean = true;
		private var rescan:Boolean = false; // Whether to rescan on next frame.
		private var rehighlight:Boolean = false; // Whether to rehighlight on next frame.
		private var charAt:int = -1; // keeps track of character index (integers below are pointers into domain memory)
		private var scanData:Vector.<LTF_FormatEntry> = new Vector.<LTF_FormatEntry>(); // Stores the data from the last scan.
		private var lastScanData:Vector.<LTF_FormatEntry> = new Vector.<LTF_FormatEntry>(); // Stores data from the scan before last.
		
		private var fontEmbedded:Boolean = false;
	
		public function LuaTextField(size:int = 12) 
		{
			super();
			this.setColors(colorDefault);
			this.embedFonts = false;
			this.defaultTextFormat = new TextFormat("Courier New", size, 0x000000);
			rescan = true;
			rehighlight = true;
			this.addEventListener(TextEvent.TEXT_INPUT, this.textInput_Listen, false, 0, true);
			this.addEventListener(Event.ENTER_FRAME, this.frame_Listen, false, 0, true);
			this.addEventListener(Event.CHANGE, this.change_Listen, false, 0, true);
			this.addEventListener(Event.SCROLL, this.scroll_Listen, false, 0, true);
		}
		
		public function set font(newFont:String) : void
		{
			this.defaultTextFormat = new TextFormat(newFont, this.defaultTextFormat.size, this.formatList[FMT_BASE].color);
			this.setTextFormat(this.defaultTextFormat);
			rehighlight = true; // our window may have changed!
		}
		
		public function get font() : String
		{
			return this.defaultTextFormat.font;
		}
		
		override public function set embedFonts(value:Boolean):void {
			super.embedFonts = value;
			this.fontEmbedded = value;
		}
		
		/*
			Adds all words in the array to the list of library words.
			If you have an active LuaTextField, you may want to force a rescan.
		*/
		public static function addLibraryWords(words:Array) : void
		{
			var i:int = 0;
			for (i = 0; i < words.length; i++) {
				IdentifierDictionary[words[i]] = IDENTIFIER_LIBWORD;
			}
		}
		
		/*
			Removes all words in the array from the list of library words.
			If you have an active LuaTextField, you may want to force a rescan.
		*/
		public static function removeLibraryWords(words:Array) : void
		{
			var i:int = 0;
			for (i = 0; i < words.length; i++) {
				delete IdentifierDictionary[words[i]];
			}
		}
		
		override public function set text(value:String):void {
			super.text = value;
			this.forceRescan();
		}
		
		public function setColors(colors:Object){
			rehighlight = true;
			if ("base" in colors) {
				this.formatList[FMT_BASE] = new TextFormat(null,null,colors.base);
				this.defaultTextFormat = new TextFormat(this.defaultTextFormat.font, this.defaultTextFormat.size, colors.base);
			}
			if ("string" in colors) {
				this.formatList[FMT_STRING_LITERAL] = new TextFormat(null,null,colors.string);
			}
			if ("number" in colors) {
				this.formatList[FMT_NUMBER_LITERAL] = new TextFormat(null,null,colors.number);
			}
			if ("keyword" in colors) {
				this.formatList[FMT_KEYWORD] = new TextFormat(null, null, colors.keyword);
			}
			if ("libword" in colors) {
				this.formatList[FMT_LIBRARY_WORD] = new TextFormat(null, null, colors.libword);
			}
			if ("comment" in colors) {
				this.formatList[FMT_COMMENT] = new TextFormat(null, null, colors.comment);
			}
		}
		
		/*
			these first indicate the 'rescan' range, and then the 'rehighlight' range.
			rescan:
				rangeStart is where we intend to start rescan. (it actually starts at the start of the range it was in last time)
				rangeEnd is where we intend to end rescan (it actually goes until the scan lines up with the old one.).
			rehighlight:
				both are updated to the actual start and end of the prior scan.
				base highlight entire range.
				highlight all non-base stuff in view.
		*/
		private var dirtyRangeStart:int = -1;
		private var dirtyRangeEnd:int = -1;
		
		// call this if you change the library word list or something.
		public function forceRescan() : void
		{
			// force a *full* rescan.
			this.dirtyRangeStart = 0;
			this.dirtyRangeEnd = this.text.length;
			this.rescan = true;
		}
		
		public function set highlightingEnabled(v:Boolean) : void
		{
			_highlightingEnabled = v;
			rescan = true;
			// unhighlight everything.
			this.dirtyRangeStart = 0;
			this.dirtyRangeEnd = this.text.length;
			this.setTextFormat(this.formatList[FMT_BASE]);
		}
		
		private var charsAdded:int = 0;
		private var previousLength:int = 0;
		private var charsOffset:int = 0; // offset of characters from old to new after end of range.
		
		private function textInput_Listen(evt:TextEvent) : void
		{
			// some information we need.
			this.charsAdded = evt.text.length;
			this.previousLength = this.text.length;
		}
		
		private function change_Listen(evt:Event) : void
		{
			var currentLength = this.text.length;
			var rangeEnd:int = this.caretIndex;
			if (this.dirtyRangeStart == -1) {
				this.charsOffset = (currentLength - previousLength);
				this.dirtyRangeStart = rangeEnd - this.charsAdded;
				this.dirtyRangeEnd = rangeEnd;
			} else { // this shit is dubious as fuck. It seems to work great though.
				var thisRangeStart = rangeEnd - this.charsAdded;
				var charDiff:int = (currentLength - previousLength);
				this.charsOffset += charDiff;
				if (rangeEnd > this.dirtyRangeEnd) {
					this.dirtyRangeEnd = rangeEnd;
				} else {
					this.dirtyRangeEnd += this.charsAdded;
				}
				if (thisRangeStart < this.dirtyRangeStart) {
					this.dirtyRangeStart = thisRangeStart;
				}
			}
			previousLength = currentLength;
			rescan = true;
		}
		
		private function scroll_Listen(evt:Event) : void
		{
			rehighlight = true;
		}
		
		private function frame_Listen(evt:Event) : void
		{
			var runtime:int;
			if (rescan) {
				runtime = getTimer();
				this.scanText();
				runtime = getTimer() - runtime;
				trace("scan: " + runtime + "ms");
			}
			if (rehighlight) {
				runtime = getTimer();
				this.highlight();
				runtime = getTimer() - runtime;
				trace("highlight: " + runtime + "ms");
			}
		}
		
		private static function seekScanIndex(scanData:Vector.<LTF_FormatEntry>, charIndex:int) : int
		{
			var lowest:int = 0;
			var highest = scanData.length - 1;
			
			while (lowest < highest) {
				var mid:int = (lowest + highest) >> 1;
				var entry:LTF_FormatEntry = scanData[mid];
				if (entry.charStart > charIndex) {
					highest = mid - 1;
				} else if (entry.charEnd <= charIndex) {
					lowest = mid + 1;
				} else { // charStart <= charIndex, charEnd > charIndex
					return mid;
				}
			}
			return lowest;
		}
		
		
		// returns pointer to the character index.
		private static function seekCharIndex(ptr:int, charIndex:int) : int
		{
			var charAt:int = 0;
			var char:int = li8(ptr);
			while (charAt < charIndex) { // we assume charIndex is in range.
				if (char >= 0xc0) { // UTF8
					if (char >= 0xf0) { // 4 byte
						ptr += 4;
					} else if (char >= 0xe0) { // 3 byte
						ptr += 3;
					} else { // 2 byte
						ptr += 2;
					}
				} else {
					ptr++;
				}
				char = li8(ptr);
				charAt++;
			}
			return ptr;
		}
		
		private function highlight() : void
		{
			if (!_highlightingEnabled) {return;}
			if (!rehighlight) {return;}
			if (this.text.length == 0) {return;}
			var didWork:Boolean = false;
			
			try{
				var toHighlight:Vector.<LTF_FormatEntry> = new Vector.<LTF_FormatEntry>();
				var minChar:int = this.getLineOffset(this.scrollV - 1);
				var maxChar:int = int.MAX_VALUE;
				if (this.bottomScrollV < this.numLines) {
					maxChar = this.getLineOffset(this.bottomScrollV) - 1;
				}
				var i:int = 0;
				var j:int = 0;
				var entry:LTF_FormatEntry;
				var minX:int = this.scrollH;
				var maxX:int = this.scrollH + this.width;
				var rect:Rectangle;
				var rect2:Rectangle;
				//trace("[");
				//trace("p1: " + getTimer());
				for (i = seekScanIndex(this.scanData,minChar); i < this.scanData.length; i++) {
					entry = this.scanData[i];
					if (entry.charEnd <= minChar) {continue;}
					if (entry.charStart >= maxChar) {break;}
					if (entry.formatType == FMT_BASE) {continue;}
					if (entry.highlighted) {continue;}
					// This shit sucks but it's the only way.
					rect = null;
					var rangeMin:int = Math.max(entry.charStart, minChar); // clamp to this range so the check does not take 1000 years.
					var rangeMax:int = Math.min(entry.charEnd, maxChar);
					for (j = rangeMin; j < rangeMax; j++) {
						rect = this.getCharBoundaries(j);
						if (rect != null) {break;}
					}
					if (rect == null) {
						//trace("  " + i + " : Fallback!")
						//trace("  [" + i + "] = " + entry.toString() + ",");
						toHighlight.push(entry);
						continue;
					}
					//trace("    part2");
					if (rect.left < maxX && rect.right > minX) {
						toHighlight.push(entry);
						continue;
					}
					for (j = rangeMax - 1; j >= rangeMin; j--) {
						rect2 = this.getCharBoundaries(j);
						if (rect2 != null) {break;}
					}
					// rect2 will have found something.
					rect = rect.union(rect2);
					//trace("    part3");
					if (rect.left > maxX || rect.right < minX) {continue;}
					toHighlight.push(entry);
				}
				//trace("]");
				//trace("p2: " + getTimer());
				if (this.dirtyRangeStart >= 0 || toHighlight.length > 0) {
					if (!this.fontEmbedded) {super.embedFonts = true;}
					didWork = true;
					if (this.dirtyRangeStart >= 0) {
						//trace(" clear range [" + this.dirtyRangeStart + ", " + this.dirtyRangeEnd + "]");
						if (toHighlight.length == 1) { // adobe scout says this was a good optimization for one specific case involving a 70k character string.
							entry = toHighlight[0];
							if (entry.charStart == this.dirtyRangeStart && entry.charEnd == this.dirtyRangeEnd) {
								// skip unhighlight
							} else if (entry.charStart == this.dirtyRangeStart) {
								this.setTextFormat(formatList[FMT_BASE], entry.charEnd, this.dirtyRangeEnd);
							} else if (entry.charEnd == this.dirtyRangeEnd) {
								this.setTextFormat(formatList[FMT_BASE], this.dirtyRangeStart, entry.charStart);
							} else {
								this.setTextFormat(formatList[FMT_BASE], this.dirtyRangeStart, this.dirtyRangeEnd);
							}
						} else {
							this.setTextFormat(formatList[FMT_BASE], this.dirtyRangeStart, this.dirtyRangeEnd);
						}
						this.dirtyRangeStart = -1; this.dirtyRangeEnd = -1;
					}
					for (i = 0; i < toHighlight.length; i++) {
						entry = toHighlight[i];
						//trace("  [" + i + "] = " + entry.toString() + ",");
						entry.highlighted = true;
						this.setTextFormat(formatList[entry.formatType], entry.charStart, entry.charEnd);
					}
				}
				//trace("p3: " + getTimer());
			} catch(e:Error) {
				trace(e.toString());
				throw e;
			}
			if (didWork && !this.fontEmbedded) {super.embedFonts = false;}
			rehighlight = false;
		}
		
		private var scanIndex:int = 0;
		private var lastScanIndex:int = -1; // index into the last scan, for comparison.
		private var spanCharStart:int = -1;
		private var spanCharEnd:int = -1;
		private var spanFormatType:int = -1;
		
		// copies over scan data, ending at the entry prior to charIndex, and returning the scan index prior to the one containing the character index.
		// that scan index is returned in case the current character should actually be contained by the previous one.
		private function copyScanData(charIndex:int) : int
		{
			var scanIndex:int = 0;
			
			while (scanIndex < lastScanData.length) {
				var entry:LTF_FormatEntry = lastScanData[scanIndex];
				if (entry.charEnd <= charIndex) {
					if (scanIndex < scanData.length) {
						this.scanData[scanIndex].copyFrom(entry);
					} else { // new one.
						this.scanData[scanIndex] = entry.clone();
					}
				} else {
					this.scanIndex = scanIndex - 1;
					if (this.scanIndex < 0) { // failsafe
						this.scanIndex = 0;
					}
					return this.scanIndex;
				}
				scanIndex++;
			}
			this.scanIndex = scanIndex - 1;
			if (this.scanIndex < 0) { // failsafe
				this.scanIndex = 0;
			}
			return this.scanIndex;
		}
		
		// copies over scan data, starting once entries line up, until the end of the old data.
		// assume both are after dirtyRangeEnd
		private function copyRemainingData(scanIndex:int, lastScanIndex:int) : void
		{
			for (var i:int = lastScanIndex; i < this.lastScanData.length; i++) {
				if (scanIndex < this.scanData.length) {
					this.scanData[scanIndex].copyFrom(this.lastScanData[i], this.charsOffset);
				} else {
					this.scanData.push(this.lastScanData[i].clone(this.charsOffset));
				}
				scanIndex++;
			}
			this.scanData.length = scanIndex;
		}
		
		// return true if lined up again.
		private function pushScanData(charStart:int, charEnd:int, formatType:int) : Boolean
		{
			if (formatType != spanFormatType) {
				if (spanFormatType != -1) {
					if (scanIndex < this.scanData.length) {
						var entry:LTF_FormatEntry = this.scanData[scanIndex];
						entry.highlighted = false;
						entry.setData(spanCharStart, spanCharEnd, spanFormatType);
						if (spanCharStart > this.dirtyRangeEnd && lastScanIndex != -2) { // -2 indicates out of range of last scan.
							if (lastScanIndex == -1) {
								lastScanIndex = seekScanIndex(this.lastScanData, spanCharStart - this.charsOffset);
							}
							// compare.
							// we advance lastScanIndex until the respective entry is out of range of this one, or there are no more entries.
							//trace("Comparing: " + entry.toString());
							//trace(" offset: " + this.charsOffset);
							var last_entry:LTF_FormatEntry = this.lastScanData[lastScanIndex];
							while (true) {
								//trace("  with: " + last_entry.toString());
								if (entry.equals(last_entry,this.charsOffset)) {
									this.dirtyRangeEnd = spanCharStart; // we can copy over the rest.
									//trace("End early");
									return true; // indicate we can stop scanning.
								}
								lastScanIndex++;
								if (lastScanIndex >= this.lastScanData.length) {lastScanIndex = -2; break;}
								last_entry = this.lastScanData[lastScanIndex];
								if ((last_entry.charStart + this.charsOffset) >= spanCharEnd) {
									// last_entry goes out of range of this entry, nothing seen, keep scanning.
									break;
								}
							}
						}
						scanIndex++;
					} else {
						this.scanData.push(new LTF_FormatEntry(spanCharStart, spanCharEnd, spanFormatType));
						scanIndex++;
					}
				}
				// start new span
				spanCharStart = charStart;
				spanCharEnd = charEnd;
				spanFormatType = formatType;
			} else {
				// extend span
				spanCharEnd = charEnd;
			}
			return false;
		}
		
		private function finalizeScanData(earlyEnd:Boolean) : void
		{
			// If earlyEnd is true, we need to copy over data.
			if (earlyEnd) {
				copyRemainingData(this.scanIndex, this.lastScanIndex);
			} else { // no early end, ended on text.
				this.dirtyRangeEnd = this.text.length;
				if (spanFormatType != -1) {
					if (scanIndex < this.scanData.length) {
						var entry:LTF_FormatEntry = this.scanData[scanIndex];
						entry.highlighted = false;
						entry.setData(spanCharStart, spanCharEnd, spanFormatType);
						scanIndex++;
						this.scanData.length = scanIndex;
					} else {
						this.scanData.push(new LTF_FormatEntry(spanCharStart, spanCharEnd, spanFormatType));
						scanIndex++;
					}
				} else {
					this.scanData.length = 0;
				}
			}
			// reset span data.
			this.scanIndex = 0;
			this.lastScanIndex = -1;
			this.spanCharStart = -1;
			this.spanCharEnd = -1;
			this.spanFormatType = -1;
		}
		
		
		
		/*
			========================
			  Actual text scanning
			========================
		*/
		
		
		
		private function scanText() : void
		{
			if (!_highlightingEnabled) {return;}
			if (!rescan) {return;}
			rescan = false;
			rehighlight = true;
			// malloc w/ CModule.
			var str_ptr:int = CModule.mallocString(this.text);
			//trace("Allocated!");
			var ptr:int = str_ptr;
			var ptr2:int = str_ptr;

			var ITER:int = 0;
			try {
				var tempVec:Vector.<LTF_FormatEntry> = this.scanData; // swap
				this.scanData = this.lastScanData;
				this.lastScanData = tempVec;
				//trace(" scan range [" + this.dirtyRangeStart + ", " + this.dirtyRangeEnd + "]");
				if (this.dirtyRangeStart > 0) {
					this.dirtyRangeStart = this.lastScanData[copyScanData(this.dirtyRangeStart)].charStart;
				} else {
					this.dirtyRangeStart = 0;
				}
				//trace("error?");
				ptr = seekCharIndex(ptr, this.dirtyRangeStart);
				this.charAt = this.dirtyRangeStart;
				var lookahead:int = 0;
				var earlyEnd:Boolean = false;
				while (true) {
					var char:int = li8(ptr);
					if (char == CHAR_EOF) {break;}
					var chartype:int = charType[char];
					var charStart:int = this.charAt;
					
					ITER++;
					if (ITER > 1000000) {throw new Error("Too many iterations (infinite loop?");}
					
					// All functions should set this.charAt to the *next* character.
					switch(chartype) {
						case CHARTYPE_OTHER: {
							ptr = this.scanToSeparator(ptr); // Whatever this is probably isn't valid, so skip to something we recognize?
							if (li8(ptr) == CHAR_EOF) { // check EOF
								this.pushScanData(charStart, this.charAt, FMT_BASE);
								break;
							}
							ptr = this.scanSeparators(ptr); // skip past the relevant separators.
							earlyEnd = this.pushScanData(charStart, this.charAt, FMT_BASE);
							break;
						}
						case CHARTYPE_SEPARATOR: {
							// Check special cases
							if (char == CHAR_DASH) {
								lookahead = li8(ptr + 1);
								if (lookahead == CHAR_DASH) {
									ptr = this.scanComment(ptr);
									earlyEnd = this.pushScanData(charStart, this.charAt, FMT_COMMENT);
									break;
								} else if (charType[lookahead] == CHARTYPE_NUMSTART) {
									ptr2 = ptr;
									var nHighlightType:int = 1;
									while (nHighlightType == 1) { // skip while it's 1.
										ptr2--;
										if (ptr2 < str_ptr) {nHighlightType = 2; break;}
										nHighlightType = negativeHighlight[li8(ptr2)];
									}
									if (nHighlightType == 2) {
										this.charAt++; // skip this dash
										ptr = this.scanNumberLiteral(ptr + 1);
										earlyEnd = this.pushScanData(charStart, this.charAt, FMT_NUMBER_LITERAL);
										break;
									} else {
										ptr++;
										this.charAt++;
										earlyEnd = this.pushScanData(charStart, this.charAt, FMT_BASE);
										break;
									}
								}
							} else if (char == CHAR_QUOTATION || char == CHAR_APOSTROPHE) {
								ptr = this.scanShortStr(ptr);
								earlyEnd = this.pushScanData(charStart, this.charAt, FMT_STRING_LITERAL);
								break;
							} else if (char == CHAR_LBRACKET) {
								lookahead = li8(ptr + 1);
								if (lookahead == CHAR_LBRACKET || lookahead == CHAR_EQUAL) {
									var ptr_end = this.scanLongStr(ptr);
									if (ptr_end != -1) {
										ptr = ptr_end;
										earlyEnd = this.pushScanData(charStart, this.charAt, FMT_STRING_LITERAL);
										break;
									} else {
										ptr++; // we skip it.
										this.charAt++;
									}
								}
							}
							ptr = this.scanSeparators(ptr);
							earlyEnd = this.pushScanData(charStart, this.charAt, FMT_BASE);
							break;
						}
						case CHARTYPE_IDSTART: {
							ptr2 = this.scanIdentifier(ptr);
							var str:String = CModule.readString(ptr, ptr2-ptr);
							ptr = ptr2;
							var id_type:* = IdentifierDictionary[str];
							if (id_type == null) {
								earlyEnd = this.pushScanData(charStart, this.charAt, FMT_BASE);
							} else {
								if (id_type is Number) {
									earlyEnd = this.pushScanData(charStart, this.charAt, id_type);
								} else { // in case someone catches some weird shit out of the dictionary idk.
									earlyEnd = this.pushScanData(charStart, this.charAt, FMT_BASE);
								}
							}
							break;
						}
						case CHARTYPE_NUMSTART: {
							ptr = this.scanNumberLiteral(ptr);
							earlyEnd = this.pushScanData(charStart, this.charAt, FMT_NUMBER_LITERAL);
							break;
						}
						default: {
							throw new Error("Invalid character type");
						}
					}
					if (earlyEnd) {break;}
				}
				this.finalizeScanData(earlyEnd);
				rehighlight = true;
			} catch(e:Error) {
				trace(e.toString());
				throw e;
			} finally {
				//trace("Freed");
				CModule.free(str_ptr);
			}
		}
		
		// All scans should return immediately if \0 is seen, with this.charAt and ptr being at the \0.
		// All scans will set this.charAt to the character after the end of their respective span.
		// UTF-8 is only handled in here and in string scans, all other scans won't cross characters over a byte.
		private function scanToSeparator(ptr:int) : int // returns ptr of first separator seen.
		{
			//trace("  scanToSeparator (ptr: " + ptr + ", charAt: " + this.charAt + ");")
			while (true) {
				var char:int = li8(ptr);
				if (char >= 0xc0) { // UTF8
					if (char >= 0xf0) { // 4 byte
						ptr += 4;
					} else if (char >= 0xe0) { // 3 byte
						ptr += 3;
					} else { // 2 byte
						ptr += 2;
					}
					this.charAt++;
				} else if (char == CHAR_EOF) {
					//trace("    end by EOF");
					return ptr;
				} else {
					if (charSeparator[char] == 1) {
						//trace("    end by separator");
						return ptr;
					}
					ptr++;
					this.charAt++;
				}
			}
		}
		
		private function scanSeparators(ptr:int) : int // returns ptr of first non-separator seen.
		{
			//trace("  scanSeparators (ptr: " + ptr + ", charAt: " + this.charAt + ");")
			ptr++;
			var char:int = li8(ptr);
			this.charAt++;
			var lookahead:int = 0;
			while (true) {
				if (charType[char] != CHARTYPE_SEPARATOR) {
					//trace("    end by non-separator");
					return ptr;
				} else {
					if (char == CHAR_DASH) {
						lookahead = li8(ptr + 1);
						if (lookahead == CHAR_DASH) {
							//trace("    end by two dashes");
							return ptr;
						} else if (charType[lookahead] == CHARTYPE_NUMSTART) {
							//trace("    end by numerical");
							return ptr;
						}
					} else if (char == CHAR_QUOTATION || char == CHAR_APOSTROPHE) {
						//trace("    end by quotation / apostrophe");
						return ptr;
					} else if (char == CHAR_LBRACKET) {
						lookahead = li8(ptr + 1)
						if (lookahead == CHAR_LBRACKET || lookahead == CHAR_EQUAL) {
							//trace("    end by possible long string");
							return ptr;
						}
					}
				}
				ptr++;
				char = li8(ptr);
				this.charAt++;
			}
		}
		
		// scans non-separating characters. I guess this is similar to scanToSeparator. We don't handle UTF-8 here though, and neither does Lua from my tests.
		// Checking if the identifier matches stuff is handled elsewhere...
		private function scanIdentifier(ptr:int) : int
		{
			//trace("  scanIdentifier (ptr: " + ptr + ", charAt: " + this.charAt + ");")
			var char:int = li8(ptr);
			while (true) {
				char = li8(ptr);
				if (charType[char] >= CHARTYPE_IDSTART) {
					ptr++;
					this.charAt++;
				} else {
					//trace("    end by non-identifier character");
					return ptr;
				}
			}
		}
		
		// scans a number literal.
		private function scanNumberLiteral(ptr:int) : int
		{
			//trace("  scanNumberLiteral (ptr: " + ptr + ", charAt: " + this.charAt + ");")
			var char:int = li8(ptr);
			var lookahead:int = 0;
			var numClass:int = NUMCLASS_DECIMAL;
			if (char == CHAR_ZERO) {
				lookahead = li8(ptr + 1);
				if (lookahead == CHAR_LOWER_X || lookahead == CHAR_UPPER_X) {
					numClass = NUMCLASS_HEX;
					ptr += 2;
					char = li8(ptr);
					this.charAt += 2;
				}
			}
			var lower_exp:int = CHAR_LOWER_E;
			var upper_exp:int = CHAR_UPPER_E;
			
			if (numClass == NUMCLASS_HEX) {
				lower_exp = CHAR_LOWER_P;
				upper_exp = CHAR_UPPER_P;
			}
			
			// stage 1: check chars, dot, or exp.
			while (true) {
				if (numeralClass[char] >= numClass) { // equal or higher (dec: >= 2, hex: >= 1)
					ptr++; char = li8(ptr); this.charAt++;
				} else if (char == CHAR_DOT) {
					break;
				} else if (char == lower_exp || char == upper_exp) {
					break;
				} else { // non numerical, bail out!
					//trace("    end by non-numerical");
					return ptr;
				}
			}
			// stage 2: after dot: check chars, exp; (Skipped if no dot)
			if (char == CHAR_DOT) {
				ptr++; char = li8(ptr); this.charAt++;
				while(true) {
					if (numeralClass[char] >= numClass) { // equal or higher (dec: >= 2, hex: >= 1)
						ptr++; char = li8(ptr); this.charAt++;
					} else if (char == lower_exp || char == upper_exp) {
						break;
					} else { // non numerical, bail out!
						//trace("    end by non-numerical");
						return ptr;
					}
				}
			}
			// stage 3: handle exp
			ptr++; char = li8(ptr); this.charAt++;
			if (char == CHAR_PLUS || char == CHAR_DASH) { // advance past plus or minus
				ptr++; char = li8(ptr); this.charAt++;
			} else if (numeralClass[char] == 0) { // malformed, end here.
				return ptr;
			}
			while (true) {
				if (numeralClass[char] >= NUMCLASS_DECIMAL) { // can only have decimal characters now.
					ptr++; char = li8(ptr); this.charAt++;
				} else {
					//trace("    end by non-numerical");
					return ptr; // must bail now.
				}
			}
		}
		
		// scans a comment. start on first '-'
		private function scanComment(ptr:int) : int
		{
			//trace("  scanComment (ptr: " + ptr + ", charAt: " + this.charAt + ");")
			var isLong:Boolean = false;
			// Need to check for long.
			ptr += 2;
			this.charAt += 2;
			var char:int = li8(ptr);
			if (char == CHAR_LBRACKET) {
				var lookahead:int = li8(ptr + 1);
				if (lookahead == CHAR_LBRACKET || lookahead == CHAR_EQUAL) {
					isLong = true;
				}
			}
			
			if (isLong) {
				var ptr_end:int = this.scanLongStr(ptr);
				if (ptr_end != -1) {
					//trace("    end by long scan end");
					return ptr_end;
				}
			}
			// scan to \n or \0.
			while (true) {
				if (char == CHAR_NEWLINE || char == CHAR_EOF) {
					//trace("    end by EOF / new line");
					return ptr;
				} else if (char >= 0xc0) { // UTF8
					if (char >= 0xf0) { // 4 byte
						ptr += 4;
					} else if (char >= 0xe0) { // 3 byte
						ptr += 3;
					} else { // 2 byte
						ptr += 2;
					}
					this.charAt++; char = li8(ptr); continue;
				}
				ptr++; char = li8(ptr); this.charAt++;
			}
		}
		
		// ptr should start on first quotation mark.
		private function scanShortStr(ptr:int) : int
		{
			//trace("  scanShortStr (ptr: " + ptr + ", charAt: " + this.charAt + ");")
			// we know this is a quotation no need to rescan.
			var delimiter:int = li8(ptr);
			ptr++;
			var char:int = li8(ptr);
			this.charAt++;
			while (true) {
				if (char == delimiter) {
					ptr++; this.charAt++;
					//trace("    end by delimiter");
					return ptr;
				} else if (char == CHAR_BACKSLASH) {
					ptr++; char = li8(ptr); this.charAt++;
					// skip past so long as not \0.
					if (char == CHAR_EOF) {
						//trace("    end by EOF");
						return ptr;
					}
				} else if (char == CHAR_EOF || char == CHAR_NEWLINE) {
					//trace("    end by EOF / new line");
					return ptr;
				} else if (char >= 0xc0) { // UTF8
					if (char >= 0xf0) { // 4 byte
						ptr += 4;
					} else if (char >= 0xe0) { // 3 byte
						ptr += 3;
					} else { // 2 byte
						ptr += 2;
					}
					this.charAt++; char = li8(ptr); continue;
				}
				ptr++;
				char = li8(ptr);
				this.charAt++;
			}
		}
		
		// ptr should start on first '['.
		// Note: This one can fail to scan, if it is not actually a long string. In this case, it returns -1.
		private function scanLongStr(ptr:int) : int //
		{
			//trace("  scanLongStr (ptr: " + ptr + ", charAt: " + this.charAt + ");")
			// known that current is '['.
			var charStore:int = this.charAt;
			ptr++; this.charAt++;
			var char:int = li8(ptr);
			var eqs:int = 0;
			while (true) { // Count equal signs.
				if (char == CHAR_LBRACKET) {
					ptr++; this.charAt++; char = li8(ptr);
					break;
				} else if (char == CHAR_EQUAL) {
					eqs++;
				} else { // Restore, fail to scan.
					this.charAt = charStore;
					//trace("    end by scan fail");
					return -1;
				}
				ptr++; this.charAt++; char = li8(ptr);
			}
			// scan until matching ending
			var end_eqs:int = 0;
			while (true) {
				if (char == CHAR_RBRACKET) {
					end_eqs = 0;
					ptr++; this.charAt++; char = li8(ptr);
					while (true) {
						if (char == CHAR_EQUAL) {
							end_eqs++;
							ptr++; this.charAt++; char = li8(ptr);
						} else {break;}
					}
					if (char == CHAR_EOF) {return ptr;}
					if (char == CHAR_RBRACKET && end_eqs == eqs) {
						ptr++; this.charAt++;
						//trace("    end by string end");
						return ptr;
					}
				} else if (char == CHAR_EOF) {
					//trace("    end by EOF");
					return ptr;
				} else if (char >= 0xc0) { // UTF8
					if (char >= 0xf0) { // 4 byte
						ptr += 4;
					} else if (char >= 0xe0) { // 3 byte
						ptr += 3;
					} else { // 2 byte
						ptr += 2;
					}
					this.charAt++; char = li8(ptr);
				} else {
					ptr++; this.charAt++; char = li8(ptr);
				}
			}
		}
	}
	
}