/*
** ADOBE SYSTEMS INCORPORATED
** Copyright 2012 Adobe Systems Incorporated
** All Rights Reserved.
**
** NOTICE:  Adobe permits you to use, modify, and distribute this file in accordance with the
** terms of the Adobe license agreement accompanying it.  If you have received this file from a
** source other than Adobe, then your use, modification, or distribution of it requires the prior
** written permission of Adobe.
*/
// Flash Runtime interop

#define lbitlib_c
#define LUA_LIB

#include <stdlib.h>
#include <string.h>

#include "AS3/AS3.h"
#include "lua.h"

#include "lauxlib.h"
#include "lualib.h"
#include "lstate.h"

// ===============================================================
//                    Flash Object Interop
// ===============================================================

static int flash_safegetprop (lua_State *L);
static int flash_safesetprop (lua_State *L);
static int flash_metacall (lua_State *L);


package_as3(
	"#package public\n"
	"import flash.utils.Dictionary;\n"
	"public var __lua_objrefs:Dictionary = new Dictionary();\n" // Keep track of object references from lua
	"public var __lua_typerefs:Dictionary = new Dictionary();\n" // Keep track of types we may want to convert, via their constructors.
	"public var __args:Array = new Array();\n" // Array used to store arguments for calling AS3 functions.
	"public dynamic class LuaResultArray extends Array {\n"
	"  public function LuaResultArray(... args){\n"
	"    this.push.apply(this, args);\n"
	"  }\n"
	"}"
);

package_as3(
	"#package private\n"
	"function pushAS3(L:int, obj:Object):void\n"
	"  {\n"
	"    if (obj is Number){\n"
	"      Lua.lua_pushnumber(L, obj as Number);\n"
	"    } else if (obj is Boolean){\n"
	"      Lua.lua_pushboolean(L, int(obj));\n"
	"    } else if (obj is String) {\n"
	"      try{\n"
	"        Lua.lua_pushstring(L, obj as String);\n"
	"      } catch(e:Error) {\n"
	"        Lua.lua_pushstring(L, \"out of memory\");\n"
	"        Lua.lua_error(L);\n"
	"      }\n"
	"    } else if (obj is Function) {\n"
	"      var fnptr:int = Lua.push_flashref(L);\n"
	"      __lua_objrefs[fnptr] = obj;\n"
	"    } else if (obj is LuaReference) {\n"
	"      Lua.lua_rawgeti(L,LuaEnums.LUA_REGISTRYINDEX,obj.ref);\n"
	"    } else if (obj == null) {\n"
	"      Lua.lua_pushnil(L);\n"
	"    } else {\n"
	"      if (__lua_objrefs[obj] == undefined) {\n"
	"        var udptr:int = Lua.push_flashref(L);\n"
	"        __lua_objrefs[udptr] = obj;\n"
	"        __lua_objrefs[obj] = udptr;\n"
	"        Lua.luaL_getsubtable(L, -1001000, \"flash_refs\");"
	"        Lua.lua_pushvalue(L,-2);"
	"        Lua.lua_rawseti(L,-2, udptr);"
	"        Lua.lua_pop(L,1);"
	"      } else {\n"
	"        Lua.luaL_getsubtable(L, -1001000, \"flash_refs\");\n"
	"        Lua.lua_rawgeti(L, -1, __lua_objrefs[obj]);\n"
	"	       Lua.lua_replace(L, -2);\n"
	"        if (Lua.lua_type(L, -1) == 0) {\n" // Apparently this can happen.
	"          Lua.lua_pop(L,1);\n"
	"          var udptr2:int = Lua.push_flashref(L);\n"
	"          __lua_objrefs[udptr2] = obj;\n"
	"          __lua_objrefs[obj] = udptr2;\n"
	"          Lua.luaL_getsubtable(L, -1001000, \"flash_refs\");"
	"          Lua.lua_pushvalue(L,-2);"
	"          Lua.lua_rawseti(L,-2, udptr2);"
	"          Lua.lua_pop(L,1);"
	"        }\n"
	"      }\n"
	"      if (obj.constructor in __lua_typerefs[L]) {\n"
	"        Lua.lua_rawgeti(L, -1001000, __lua_typerefs[L][obj.constructor]);\n"
	"        Lua.lua_pushvalue(L, -2);\n"
	"        Lua.lua_pcallk(L, 1, 1, 0, 0, null);\n"
	"        Lua.lua_replace(L, -2);\n"
	"      }\n"
	"    }\n"
	"}"
);


#define FlashObjectType "flash"
#define FlashObj unsigned int

static int flashref = -1;

static int typeerror (lua_State *L, int narg, const char *tname) {
	const char *msg = lua_pushfstring(L, "%s expected, got %s",
																		tname, luaL_typename(L, narg));
	return luaL_argerror(L, narg, msg);
}

void clean_flashstate()
{
	flashref = -1;
}

static lua_State* getMainThread(lua_State *L) {
	return G(L)->mainthread;
}

static void *luaL_testflashudata (lua_State *L, int ud) {
	void *p = lua_touserdata(L, ud);
	if (p != NULL) {  /* value is a userdata? */
		if (lua_getmetatable(L, ud)) {  /* does it have a metatable? */
			if(flashref == -1) {
				luaL_getmetatable(L, FlashObjectType);
				flashref = luaL_ref(L, LUA_REGISTRYINDEX);
			}
			lua_rawgeti(L, LUA_REGISTRYINDEX, flashref);
			if (!lua_rawequal(L, -1, -2)) { /* not the same? */
				p = NULL;  /* value is a userdata with wrong metatable */
			}
			lua_pop(L, 2);  /* remove both metatables */
			return p;
		}
	}
	return NULL;  /* value is not a userdata with a metatable */
}

static FlashObj* getObjRef(lua_State *L, int idx)
{
	FlashObj* r = (FlashObj*)luaL_testflashudata(L, idx);
	if (r == NULL) typeerror(L, idx, FlashObjectType);
	return r;
}

static int FlashObj_gc(lua_State *L)
{
	FlashObj *obj = (FlashObj*) lua_touserdata(L, 1); // in what world is this not a flash userdata?
	//inline_as3("trace(\"gc: \" + %0);\n" :  : "r"(obj));
	inline_as3("if (__lua_objrefs[__lua_objrefs[%0]] == %0) {delete __lua_objrefs[__lua_objrefs[%0]];}\n" : : "r"(obj));
	inline_as3("delete __lua_objrefs[%0];\n" : : "r"(obj));
	lua_pop(L, 1);
	return 0;
}

static int FlashObj_tostring(lua_State *L)
{
	FlashObj obj = (FlashObj) getObjRef(L, 1);
	char *str = NULL;
	lua_pop(L, 1);
	inline_as3(
		"try{\n"
		"  %0 = CModule.mallocString(\"\"+__lua_objrefs[%1]);\n" 
		"} catch(e:Error) {"	
		"}"
		: "=r"(str) 
		: "r"(obj)
	);
	if (str == NULL) {
		return luaL_error(L,"out of memory");
	}
	lua_pushfstring(L, "%s", str);
	free(str);
	return 1;
}

static int FlashObj_emptyIter(lua_State *L){
	lua_pushnil(L);
	lua_pushnil(L);
	return 2;
}

static int FlashObj_ipairsIter(lua_State *L){ // Called repeatedly to produce key, value pairs.
	FlashObj *obj = (FlashObj*) lua_touserdata(L, lua_upvalueindex(1)); // Guaranteed to be flash userdata.
	int thisIndex = lua_tointeger(L, lua_upvalueindex(2));
	int thisValid = 0;
	inline_as3(
		"var obj:Object = __lua_objrefs[%2];\n"
		"%0 = (int) (%1 < obj.length);\n"
		: "=r"(thisValid)
		: "r"(thisIndex), "r"((int)obj)
	);
	if (thisValid != 0) {
		lua_pushinteger(L, thisIndex + 1);
		lua_replace(L, lua_upvalueindex(2)); // Increment thisIndex closure.
		lua_pushinteger(L, thisIndex);
		inline_as3(
			"pushAS3(%0, obj[%1]);\n"
			:
			: "r"((int) L), "r"(thisIndex)
		);
	} else {
		lua_pushnil(L);
		lua_pushnil(L);
	}
	return 2;
}

static int FlashObj_ipairs(lua_State *L)
{
	FlashObj *obj = (FlashObj*) lua_touserdata(L, 1); // Guaranteed to be flash userdata. I think.
	// We return 3 things. FlashObj_pairsIter (closure w/ object), nil, nil.
	// Push flash object. Push index 0. go.
	int canIterate = 0; // 1 if can iterate.
  inline_as3(
		"var obj:Object = __lua_objrefs[%1];\n"
		"%0 = (int)(obj is Array\n"
		" || obj is Vector.<*> \n"
    " || obj is Vector.<Number>\n"
    " || obj is Vector.<int>\n"
    " || obj is Vector.<uint>);\n"
		: "=r"(canIterate)
		: "r"((int) obj)
	);
	if (canIterate != 0) {
		lua_pushvalue(L, 1);
		lua_pushinteger(L, 0);
		lua_pushcclosure(L, FlashObj_ipairsIter, 2);
	} else {
		lua_pushcfunction(L, FlashObj_emptyIter);
	}
	lua_pushnil(L);
	lua_pushnil(L);
	return 3;
}

static int FlashObj_pairsIter(lua_State *L){ // Called repeatedly to produce key, value pairs.
	FlashObj *obj = (FlashObj*) lua_touserdata(L, lua_upvalueindex(1)); // Guaranteed to be flash userdata.
	int thisIndex = lua_tointeger(L, lua_upvalueindex(2));
	int newIndex;
	inline_as3(
		"import avm2.intrinsics.iteration.*;\n"
		"var obj:Object = __lua_objrefs[%1];\n"
		"%0 = hasnext(obj, %2);\n"
		: "=r"(newIndex)
		: "r"((int) obj), "r"(thisIndex)
	);
	lua_pushinteger(L, newIndex);
	lua_replace(L, lua_upvalueindex(2));
	inline_as3(
		"import avm2.intrinsics.iteration.*;\n"
		"pushAS3(%0, nextname(obj, %1));\n"
		"pushAS3(%0, nextvalue(obj, %1));\n"
		:
		: "r"((int) L), "r"(newIndex)
	);
	return 2;
}

static int FlashObj_pairs(lua_State *L){
	// We return 3 things. FlashObj_pairsIter (closure w/ object), nil, nil.
	// Push flash object. Push index 0. go.
	lua_pushvalue(L, 1);
	lua_pushinteger(L, 0);
	lua_pushcclosure(L, FlashObj_pairsIter, 2);
	lua_pushnil(L);
	lua_pushnil(L);
	return 3;
}

static int FlashObj_len(lua_State *L){
	FlashObj *obj = (FlashObj*) lua_touserdata(L, 1);
	int length = 0;
  inline_as3(
		"var obj:Object = __lua_objrefs[%1];\n"
		"if	(obj is Array\n"
		" || obj is Vector.<*> \n"
    " || obj is Vector.<Number>\n"
    " || obj is Vector.<int>\n"
    " || obj is Vector.<uint>) {\n"
		"  %0 = obj.length;\n"
		"} else {\n"
		"  %0 = 0;\n"
		"}\n"
		: "=r"(length)
		: "r"((int) obj)
	);
	lua_pushinteger(L, length);
	return 1;
}

static const luaL_Reg FlashObj_meta[] = {
	{"__gc",        FlashObj_gc},
	{"__tostring",  FlashObj_tostring},
	{"__index",     flash_safegetprop},
	{"__newindex",  flash_safesetprop},
	{"__call",      flash_metacall},
	{"__pairs",     FlashObj_pairs},
	{"__ipairs",    FlashObj_ipairs},
	{"__len",       FlashObj_len},
	
	{NULL, NULL}
};

FlashObj* push_newflashref(lua_State *L)
{
	// Push the new userdata onto the stack
	FlashObj *result = (FlashObj*)lua_newuserdata(L, sizeof(FlashObj));
	luaL_getmetatable(L, FlashObjectType);
	lua_setmetatable(L, -2);

	luaL_getsubtable(L, LUA_REGISTRYINDEX, "flash_refs");
	lua_pushvalue(L,-2);
	lua_rawseti(L,-2, (FlashObj) result); // Store a reference
	lua_pop(L,1);
	//inline_nonreentrant_as3("trace(\"ref \" + %0);\n" :  : "r"(result));
	return result;
}

// ===============================================================
//                    Flash API Interop
// ===============================================================

static int flash_trace (lua_State *L) {
	size_t l;
	const char *s = luaL_checklstring(L, 1, &l);
	AS3_DeclareVar(str, String);
	AS3_CopyCStringToVar(str, s, l);
	lua_pop(L, 1);
	inline_nonreentrant_as3("trace(str);\n");
	return 1;
}

static int flash_gettimer (lua_State *L) {
	int result;
	inline_nonreentrant_as3("import flash.utils.getTimer;\n%0 = getTimer();\n" : "=r"(result));
	lua_pushinteger(L, result);
	return 1;
}

// Object construction

static int flash_new (lua_State *L) {
	int top = lua_gettop(L);

	size_t l;
	const char *s1 = luaL_checklstring(L, 1, &l);
	AS3_DeclareVar(classname, String);
	AS3_CopyCStringToVar(classname, s1, l);

	inline_as3("import flash.utils.getDefinitionByName;\n");
	inline_as3("var clz:Class = getDefinitionByName(classname);\n");
	inline_as3("var args:Array = [];\n");

	int i = 2;
	while(i <= top) {
		switch(lua_type(L, i)) {
			case LUA_TBOOLEAN: inline_as3("args.push(%0);\n" : : "r"(lua_toboolean(L, i))); break;
			case LUA_TNUMBER: inline_as3("args.push(%0);\n" : : "r"(lua_tonumber(L, i))); break;
			case LUA_TFUNCTION:
			{
				lua_settop(L, top+1);
				lua_copy(L, i, top+1);
				int ref = luaL_ref(L, LUA_REGISTRYINDEX);
				lua_settop(L, top);
				AS3_DeclareVar(luastate, int);
				AS3_CopyScalarToVar(luastate, L);
				inline_as3(
				"args.push(function(...vaargs):void"
				"{"
				"  Lua.lua_rawgeti(luastate, %1, %0);"
				"  for(var i:int = 0; i<vaargs.length;i++) {"
				"    var udptr:int = Lua.push_flashref(luastate);"
				"    __lua_objrefs[udptr] = vaargs[i];"
				"  };"
				"  Lua.lua_callk(luastate, vaargs.length, 0, 0, null);"
				"});" : : "r"(ref), "r"(LUA_REGISTRYINDEX));
				break;
			}
			case LUA_TUSERDATA: inline_as3("args.push(__lua_objrefs[%0]);\n" : : "r"(luaL_checkudata(L, i, FlashObjectType))); break;
			case LUA_TSTRING:
			{
				const char *s = luaL_checklstring(L, i, &l);
				AS3_DeclareVar(strvar, String);
				AS3_CopyCStringToVar(strvar, s, l);
				inline_as3("args.push(strvar);\n");
				break;
			}
			default:
				inline_as3("trace(\"unknown: \" + %0 + \",\" + %1+ \",\" + %2);\n" :  : "r"(i), "r"(top), "r"(lua_type(L, i)));
				return 0;
		}
		i++;
	}
	
	// Flush all args off the stack
	lua_pop(L, top);

	// Push the new userdata onto the stack
	FlashObj *result = push_newflashref(L);

	inline_as3("switch(args.length) { \n"
		"  case 0: __lua_objrefs[%0] = new clz(); break;\n"
		"  case 1: __lua_objrefs[%0] = new clz(args[0]); break;\n"
		"  case 2: __lua_objrefs[%0] = new clz(args[0], args[1]); break;\n"
		"  case 3: __lua_objrefs[%0] = new clz(args[0], args[1], args[2]); break;\n"
		"  case 4: __lua_objrefs[%0] = new clz(args[0], args[1], args[2], args[3]); break;\n"
		"  case 5: __lua_objrefs[%0] = new clz(args[0], args[1], args[2], args[3], args[4]); break;\n" 
		"  case 6: __lua_objrefs[%0] = new clz(args[0], args[1], args[2], args[3], args[4], args[5]); break;\n"
		"  case 7: __lua_objrefs[%0] = new clz(args[0], args[1], args[2], args[3], args[4], args[5], args[6]); break;\n"
		"  case 8: __lua_objrefs[%0] = new clz(args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7]); break;\n"
		"  case 9: __lua_objrefs[%0] = new clz(args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8]); break;\n"
		"};\n"
		: : "r"(result)
	);
	inline_as3("__lua_objrefs[__lua_objrefs[%0]] = %0;\n" : : "r"(result));
	return 1;
}


// method calls

static int flash_closure_apply (lua_State *L) {
	int top = lua_gettop(L);

	FlashObj *funcobj = (FlashObj*) lua_touserdata(L, lua_upvalueindex(2)); // yes this order is weird its whatever.
	FlashObj *thisobj = (FlashObj*) lua_touserdata(L, lua_upvalueindex(1)); // These are also guaranteed to be flash userdata.

	//inline_as3("var args:Array = [];\n");
	int madeRefArray = 0;
	inline_as3("var refs:Array = null;\n");

	int index = 0;
	int i = 1;
	while(i <= top) {
		switch(lua_type(L, i)) {
			case LUA_TNIL: inline_as3("__args[%0] = (null);\n" : : "r"(index) ); break;
			case LUA_TBOOLEAN: inline_as3("__args[%1] = Boolean(%0);\n" : : "r"(lua_toboolean(L, i)), "r"(index)); break;
			case LUA_TNUMBER: inline_as3("__args[%1] = %0;\n" : : "r"(lua_tonumber(L, i)), "r"(index)); break;
			case LUA_TFUNCTION:
			case LUA_TTABLE:
			case LUA_TTHREAD:
			{
				lua_pushvalue(L,i);
				int ref = luaL_ref(L,LUA_REGISTRYINDEX);
				inline_as3("var luaRef:* = new LuaReference(%0, %1);\n" : : "r"(L), "r"(ref)); // Can't explicitly type luaRef or LLVM gets pissed.
				if (madeRefArray == 0) {madeRefArray = 1; inline_as3("refs = [];");}
				inline_as3("__args[%0] = luaRef; refs.push(luaRef);\n" : : "r"(index) );
				break;
			}
			case LUA_TUSERDATA: 
			{
				void* ud = lua_touserdata(L, i);
				int isFlash = 0;
				inline_as3("%0 = (int) (%1 in __lua_objrefs);\n" : "=r"(isFlash) : "r"((int) ud));
				if (isFlash) {
					inline_as3("__args[%1] = __lua_objrefs[%0];\n" : : "r"(getObjRef(L, i)), "r"(index));
					break;
				} else {
					isFlash = luaL_getmetafield(L, i, "__as3");
					if (isFlash) { // Conversion to AS3 exists.
						lua_pushvalue(L, i);
						lua_call(L, 1, 1);
						inline_as3("__args[%1] = __lua_objrefs[%0];\n" : : "r"(lua_touserdata(L, -1)), "r"(index));
						lua_pop(L, 1);
					} else { // we convert as a LuaReference
						lua_pushvalue(L, i);
						int uRef = luaL_ref(L,LUA_REGISTRYINDEX);
						inline_as3("var udRef:* = new LuaReference(%0, %1);\n" : : "r"(L), "r"(uRef));
						if (madeRefArray == 0) {madeRefArray = 1; inline_as3("refs = [];");}
						inline_as3("__args[%0] = udRef; refs.push(udRef);\n" : : "r"(index));
					}
				}
				break;
			}
			case LUA_TSTRING:
			{
				size_t l=0;
				const char *s = lua_tolstring(L, i, &l);
				AS3_DeclareVar(strvar, String);
				AS3_CopyCStringToVar(strvar, s, l);
				inline_nonreentrant_as3("__args[%0] = strvar;\n" : : "r"(index));
				break;
			}
			default:
				inline_as3("trace(\"unknown: \" + %0 + \",\" + %1+ \",\" + %2);\n" :  : "r"(i), "r"(top), "r"(lua_type(L, i)));
				return 0;
		}
		i++;
		index++;
	}
	// Flush all args off the stack
	lua_pop(L, top);

	AS3_DeclareVar(result, Object);
	int err = 0; // set to 1 if error.

	inline_as3(
		"try{\n"
		"  switch(%3) { \n"
		"    case 0: result = __lua_objrefs[%1](); break;\n"
		"    case 1: result = __lua_objrefs[%1](__args[0]); break;\n"
		"    case 2: result = __lua_objrefs[%1](__args[0], __args[1]); break;\n"
		"    case 3: result = __lua_objrefs[%1](__args[0], __args[1], __args[2]); break;\n"
		"    case 4: result = __lua_objrefs[%1](__args[0], __args[1], __args[2], __args[3]); break;\n"
		"    case 5: result = __lua_objrefs[%1](__args[0], __args[1], __args[2], __args[3], __args[4]); break;\n" 
		"    case 6: result = __lua_objrefs[%1](__args[0], __args[1], __args[2], __args[3], __args[4], __args[5]); break;\n"
		"    case 7: result = __lua_objrefs[%1](__args[0], __args[1], __args[2], __args[3], __args[4], __args[5], __args[6]); break;\n"
		"    case 8: result = __lua_objrefs[%1](__args[0], __args[1], __args[2], __args[3], __args[4], __args[5], __args[6], __args[7]); break;\n"
		"    case 9: result = __lua_objrefs[%1](__args[0], __args[1], __args[2], __args[3], __args[4], __args[5], __args[6], __args[7], __args[8]); break;\n"
		"    default: result = __args.length = %3; __lua_objrefs[%1].apply(__lua_objrefs[%2],__args); break;\n"
		"  };\n"
		"} catch(e : Error) {\n"
		"  %0 = 1;\n"
		"  result = e.message;\n"
		"} finally {\n"
		"  if (refs != null) {\n"
		"    for (var i:int = 0; i < refs.length; i++){\n" // Decrement reference count for any LuaReferences
		"      refs[i].decRef();"
		"    }\n"
		"  }\n"
		"}\n"
		: "=r" (err)
		: "r"(funcobj), "r"(thisobj), "r"(top)
	);

	if (err == 1){ // There was an error!
		char *errmsg = NULL;
		inline_as3(
			"try{\n"
			"  %0 = CModule.mallocString(\"\"+ result as String);\n" 
			"} catch(e:Error) {}\n"
			: "=r"(errmsg)
			: 
		);
		if (errmsg == NULL) {luaL_error(L, "out of memory");}
		lua_pushstring(L, errmsg);
		free(errmsg);
		return luaL_error(L, "AS3 Error: %s", lua_tostring(L,-1));
	}

	int type = 0;
	inline_as3(
		"if (result is Number) {%0 = 1;}"
		"else if (result is Boolean) {%0 = 2;}"
		"else if (result is String) {%0 = 3;}"
		"else if (result is Function) {%0 = 4;}"
		"else if (result == null || result == undefined) {%0 = 5;}"
		"else if (result is LuaReference) {%0 = 6;}"
		"else {%0 = 0;}\n" : "=r"(type) : );

	switch (type) {
		case 1: // Is number
			;
			lua_Number num = 0.0;
			inline_as3("%0 = result;\n" : "=r"(num) : );
			lua_pushnumber(L, num);
			break;
		case 2: // Is bool
			;
			int bool_ = 0;
			inline_as3("%0 = int(result);\n" : "=r"(bool_) : );
			lua_pushboolean(L, bool_);
			break;
		case 3: // Is string
			;
			char *str = NULL;
			inline_as3(
				"try{\n"
				"  %0 = CModule.mallocString(\"\"+ result as String);\n" 
				"} catch(e:Error) {}\n"
				: "=r"(str)
				: 
			);
			if (str == NULL) {
				luaL_error(L, "out of memory");
			}
			lua_pushfstring(L, "%s", str);
			free(str);
			break;
		case 4: // Wtf?
			lua_pushvalue(L,lua_upvalueindex(1));
			FlashObj *resFun = push_newflashref(L);
			inline_as3("__lua_objrefs[%0] = result;\n" : : "r"(resFun) );
			inline_as3("__lua_objrefs[result] = %0;\n" : : "r"(resFun) );
			lua_pushcclosure(L,flash_closure_apply,2);
			break;
		case 5: // it's null.
			lua_pushnil(L);
			break;
		case 6:
			; // right.
			int refNum = LUA_NOREF;
			inline_as3("%0 = (result as LuaReference).ref;\n" : "=r"(refNum) : );
			if (refNum == LUA_NOREF) {
				return luaL_error(L,"Received freed LuaReference"); // This should never be the case.
			} else {
				lua_rawgeti(L,LUA_REGISTRYINDEX,refNum);
			}
			inline_as3("(result as LuaReference).decRef();");
			break;
		case 0: // Ok we'll push your god damn flash reference fine
			;
			int u_ref = LUA_NOREF;
			inline_as3(
				"if (result in __lua_objrefs) {\n"
				"  %0 = __lua_objrefs[result];\n"
				"} else {\n"
				"  %0 = -2;\n"
				"}\n" : "=r"(u_ref) : 
			);

			if (u_ref == LUA_NOREF) {
				// Push the new userdata onto the stack
				FlashObj *as3udata = push_newflashref(L);
				
				// Get the prop, and store it with the new key
				inline_as3("__lua_objrefs[%0] = result;\n" : : "r"(as3udata));
				inline_as3("__lua_objrefs[result] = %0;\n" : : "r"(as3udata));
			} else { // We already have this object...
				luaL_getsubtable(L, LUA_REGISTRYINDEX, "flash_refs");
				lua_rawgeti(L,-1, u_ref);
				lua_replace(L,-2);
				if (lua_type(L,-1) == LUA_TNIL) { // Got trolled...
					lua_pop(L,1);
					// Push the new userdata onto the stack
					FlashObj *result = push_newflashref(L);
					
					// Get the prop, and store it with the new key
					inline_as3("__lua_objrefs[%0] = result;\n" : : "r"(result));
					inline_as3("__lua_objrefs[result] = %0;\n" : : "r"(result));
				}
			}

			int t_ref = LUA_NOREF;
			inline_as3(
				"if (result.constructor in __lua_typerefs[%1]) {\n"
				"  %0 = int(__lua_typerefs[%1][result.constructor]);\n"
				"} else {\n" 
				"  %0 = -2;\n"
				"}\n"
				: "=r"(t_ref)
				: "r"(getMainThread(L))
			);
			if (t_ref != LUA_NOREF) {
				lua_rawgeti(L, LUA_REGISTRYINDEX, t_ref); // function (At index 2?)
				lua_pushvalue(L,-2);
				lua_pcall(L, 1, LUA_MULTRET, 0);
				return lua_gettop(L) - 1;
			}
			break;
	}

	return 1;
}

static int flash_metacall (lua_State *L) {
	int top = lua_gettop(L);

	FlashObj *funcobj = (FlashObj*) lua_touserdata(L, 1); // yes this order is weird its whatever.

	int isFunction = 0;

	inline_as3("%0 = int(__lua_objrefs[%1] is Function);\n" : "=r"(isFunction) : "r"(funcobj));

	if (isFunction == 0) {lua_pop(L, top); return 0;} // Do nothing if not a function.

	inline_as3("var args:Array = [];\n");
	inline_as3("var refs:Array = [];\n");

	int i = 2;
	while(i <= top) {
		switch(lua_type(L, i)) {
			case LUA_TNIL: inline_as3("args.push(null);\n" : : ); break;
			case LUA_TBOOLEAN: inline_as3("args.push(%0);\n" : : "r"(lua_toboolean(L, i))); break;
			case LUA_TNUMBER: inline_as3("args.push(%0);\n" : : "r"(lua_tonumber(L, i))); break;
			case LUA_TFUNCTION:
			case LUA_TTABLE:
			case LUA_TTHREAD:
			{
				lua_pushvalue(L,i);
				int ref = luaL_ref(L,LUA_REGISTRYINDEX);
				inline_as3("var luaRef:* = new LuaReference(%0, %1);\n" : : "r"(L), "r"(ref)); // Can't explicitly type luaRef or LLVM gets pissed.
				inline_as3("args.push(luaRef); refs.push(luaRef);\n" : : );
				break;
			}
			case LUA_TUSERDATA: 
			{
				void* ud = lua_touserdata(L, i);
				int isFlash = 0;
				inline_as3("%0 = (int) (%1 in __lua_objrefs);\n" : "=r"(isFlash) : "r"((int) ud));
				if (isFlash) {
					inline_as3("args.push(__lua_objrefs[%0]);\n" : : "r"(getObjRef(L, i)));
					break;
				} else {
					isFlash = luaL_getmetafield(L, i, "__as3");
					if (isFlash) { // Conversion to AS3 exists.
						lua_pushvalue(L, i);
						lua_call(L, 1, 1);
						inline_as3("args.push(__lua_objrefs[%0]);\n" : : "r"(lua_touserdata(L, -1)));
						lua_pop(L, 1);
					} else { // we convert as a LuaReference
						lua_pushvalue(L, i);
						int uRef = luaL_ref(L,LUA_REGISTRYINDEX);
						inline_as3("var udRef:* = new LuaReference(%0, %1);\n" : : "r"(L), "r"(uRef));
						inline_as3("args.push(udRef); refs.push(udRef);\n" : : );
					}
				}
				break;
			}
			case LUA_TSTRING:
			{
				size_t l=0;
				const char *s = lua_tolstring(L, i, &l);
				AS3_DeclareVar(strvar, String);
				AS3_CopyCStringToVar(strvar, s, l);
				inline_as3("args.push(strvar);\n");
				break;
			}
			default:
				inline_as3("trace(\"unknown: \" + %0 + \",\" + %1+ \",\" + %2);\n" :  : "r"(i), "r"(top), "r"(lua_type(L, i)));
				return 0;
		}
		i++;
	}
	// Flush all args off the stack
	lua_pop(L, top);


	AS3_DeclareVar(result, Object);
	int err = 0; // set to 1 if error.

	inline_as3(
		"try{\n"
		"  switch(args.length) { \n"
		"    case 0: result = __lua_objrefs[%1](); break;\n"
		"    case 1: result = __lua_objrefs[%1](args[0]); break;\n"
		"    case 2: result = __lua_objrefs[%1](args[0], args[1]); break;\n"
		"    case 3: result = __lua_objrefs[%1](args[0], args[1], args[2]); break;\n"
		"    case 4: result = __lua_objrefs[%1](args[0], args[1], args[2], args[3]); break;\n"
		"    case 5: result = __lua_objrefs[%1](args[0], args[1], args[2], args[3], args[4]); break;\n" 
		"    case 6: result = __lua_objrefs[%1](args[0], args[1], args[2], args[3], args[4], args[5]); break;\n"
		"    case 7: result = __lua_objrefs[%1](args[0], args[1], args[2], args[3], args[4], args[5], args[6]); break;\n"
		"    case 8: result = __lua_objrefs[%1](args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7]); break;\n"
		"    case 9: result = __lua_objrefs[%1](args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8]); break;\n"
		"    default: result = __lua_objrefs[%1].apply(__lua_objrefs[%1],args); break;\n"
		"  };\n"
		"} catch(e : Error) {\n"
		"  %0 = 1;\n"
		"  result = e.message;\n"
		"} finally {\n"
		"  for (var i:int = 0; i < refs.length; i++){\n" // Decrement reference count for any LuaReferences
		"    refs[i].decRef();"
		"  }\n"
		"}\n"
		: "=r"(err) : "r"(funcobj)
	);

	if (err == 1){ // There was an error!
		char *errmsg = NULL;
		inline_as3(
			"try{\n"
			"  %0 = CModule.mallocString(\"\"+ result as String);\n" 
			"} catch(e:Error) {}\n"
			: "=r"(errmsg)
			: 
		);
		if (errmsg == NULL) {
			luaL_error(L, "out of memory");
		}
		lua_pushstring(L, errmsg);
		free(errmsg);
		return luaL_error(L, "AS3 Error: %s", lua_tostring(L,-1));
	}

	int type = 0;
	inline_as3(
		"if (result is Number) {%0 = 1;}"
		"else if (result is Boolean) {%0 = 2;}"
		"else if (result is String) {%0 = 3;}"
		"else if (result is Function) {%0 = 4;}"
		"else if (result == null || result == undefined) {%0 = 5;}"
		"else if (result is LuaReference) {%0 = 6;}"
		"else {%0 = 0;}" : "=r"(type) : );

	switch (type) {
		case 1: // Is number
			;
			lua_Number num = 0.0;
			inline_as3("%0 = result;\n" : "=r"(num) : );
			lua_pushnumber(L, num);
			break;
		case 2: // Is bool
			;
			int bool_ = 0;
			inline_as3("%0 = int(result);\n" : "=r"(bool_) : );
			lua_pushboolean(L, bool_);
			break;
		case 3: // Is string
			;
			char *str = NULL;
			inline_as3(
				"try{\n"
				"  %0 = CModule.mallocString(\"\"+ result as String);\n" 
				"} catch(e:Error) {}\n"
				: "=r"(str)
				: 
			);
			if (str == NULL) {
				luaL_error(L, "out of memory");
			}
			lua_pushfstring(L, "%s", str);
			free(str);
			break;
		case 4: // Wtf?
			lua_pushvalue(L,lua_upvalueindex(1));
			FlashObj *resFun = push_newflashref(L);
			inline_as3("__lua_objrefs[%0] = result;\n" : : "r"(resFun) );
			lua_pushcclosure(L,flash_closure_apply,2);
			break;
		case 5: // it's null.
			lua_pushnil(L);
			break;
		case 6:
			; // right.
			int refNum = LUA_NOREF;
			inline_as3("%0 = (result as LuaReference).ref;\n" : "=r"(refNum) : );
			if (refNum == LUA_NOREF) {
				return luaL_error(L,"Received freed LuaReference"); // This should never be the case.
			} else {
				lua_rawgeti(L,LUA_REGISTRYINDEX,refNum);
			}
			inline_as3("(result as LuaReference).decRef();");
			break;
		case 0: // Ok we'll push your god damn flash reference fine
			;
			int u_ref = LUA_NOREF;
			inline_as3(
				"if (result in __lua_objrefs) {\n"
				"  %0 = __lua_objrefs[result];\n"
				"} else {\n"
				"  %0 = -2;\n"
				"}\n" : "=r"(u_ref) : 
			);

			if (u_ref == LUA_NOREF) {
				// Push the new userdata onto the stack
				FlashObj *as3udata = push_newflashref(L);
				
				// Get the prop, and store it with the new key
				inline_as3("__lua_objrefs[%0] = result;\n" : : "r"(as3udata));
				inline_as3("__lua_objrefs[result] = %0;\n" : : "r"(as3udata));
			} else { // We already have this object...
				luaL_getsubtable(L, LUA_REGISTRYINDEX, "flash_refs");
				lua_rawgeti(L,-1, u_ref);
				lua_replace(L,-2);
				if (lua_type(L,-1) == LUA_TNIL) { // Got trolled...
					lua_pop(L,1);
					// Push the new userdata onto the stack
					FlashObj *result = push_newflashref(L);
					
					// Get the prop, and store it with the new key
					inline_as3("__lua_objrefs[%0] = result;\n" : : "r"(result));
					inline_as3("__lua_objrefs[result] = %0;\n" : : "r"(result));
				}
			}

			int t_ref = LUA_NOREF;
			inline_as3(
				"if (result.constructor in __lua_typerefs[%1]) {\n"
				"  %0 = int(__lua_typerefs[%1][result.constructor]);\n"
				"} else {\n" 
				"  %0 = -2;\n"
				"}\n"
				: "=r"(t_ref)
				: "r"(getMainThread(L))
			);
			if (t_ref != LUA_NOREF) {
				lua_rawgeti(L, LUA_REGISTRYINDEX, t_ref); // function (At index 2?)
				lua_pushvalue(L,-2);
				lua_pcall(L, 1, LUA_MULTRET, 0);
				return lua_gettop(L) - 1;
			}
			break;
	}

	return 1;
}

static int flash_safegetprop(lua_State *L) {
	FlashObj *obj = (FlashObj*) lua_touserdata(L, 1); // Guaranteed to be flash reference, only accessible via metamethod
	size_t l;
	const char *s = luaL_checklstring(L, 2, &l);
	AS3_DeclareVar(propname, String);
	AS3_CopyCStringToVar(propname, s, l);
	
	lua_pop(L, 1); // Pop string
	AS3_DeclareVar(o1, Object);
	//inline_as3("%0 = int(__lua_objrefs[%1].hasOwnProperty(propname));\n" : "=r"(hasProperty) : "r"(obj));
	inline_as3("o1 = __lua_objrefs[%0];\n" : : "r"(obj));
	if (1) {
		// I'm not reindenting all this shit and I want to replace it soon anyways. (surely that justifies it?)
		AS3_DeclareVar(o2, Object);
		inline_as3(
			"try{"
			"	o2 = o1[propname];\n" 
			"} catch (e:Error) {\n" // silence error.
			"}"
			: : 
		);
		int type = 0;

		inline_as3(
			"if (o2 is Number) {%0 = 1;}"
			"else if (o2 is Boolean) {%0 = 2;}"
			"else if (o2 is String) {%0 = 3;}"
			"else if (o2 is Function) {%0 = 4;}"
			"else if (o2 == null || o2 == undefined) {%0 = 5;}"
			"else if (o2 is LuaReference) {%0 = 6;}"
			"else {%0 = 0;}" : "=r"(type) : 
		);
		/*
		inline_as3(
			"if (o2 == null || o2 == undefined) {%0 = 5;}\n"
			"var t:Object = __lua_typerefs[o2.constructor];\n"
			"%0 = t == undefined ? 0 : (t as int);\n"
			: "=r"(type)
			:
		);
		*/
		switch (type) {
			case 1: // Is number
				lua_pop(L,1);
				lua_Number num;
				inline_as3("%0 = o2;\n" : "=r"(num) : );
				lua_pushnumber(L, num);
				break;
			case 2: // Is bool
				lua_pop(L,1);
				int bool_;
				inline_as3("%0 = int(o2);\n" : "=r"(bool_) : );
				lua_pushboolean(L, bool_);
				break;
			case 3: // Is string
				lua_pop(L,1);
				char *str = NULL;
				inline_as3(
					"try{\n"
					"  %0 = CModule.mallocString(\"\"+ o2 as String);\n" 
					"} catch(e:Error) {}\n"
					: "=r"(str)
					: 
				);
				if (str == NULL) {
					luaL_error(L, "out of memory");
				}
				lua_pushstring(L, str); 
				free(str);
				break;
			case 4: // Its a function
				// Don't pop the object, we need it for the closure.
				;;;;;; // Are you fucking kidding me shut the fuck up heres you're fucking expresisons
				FlashObj *fres = push_newflashref(L);
				// Get the prop, and store it with the new key
				inline_as3("__lua_objrefs[%0] = o2;\n" : : "r"(fres) );
				lua_pushcclosure(L,flash_closure_apply,2);
				break;
			case 5: // Null or undefined.. either one
				lua_pop(L,1);
				lua_pushnil(L);
				break;
			case 6:
				; // right.
				int refNum = LUA_NOREF;
				inline_as3("%0 = (o2 as LuaReference).ref;\n" : "=r"(refNum) : );
				lua_rawgeti(L,LUA_REGISTRYINDEX,refNum);
				inline_as3("(o2 as LuaReference).decRef();\n");
				break;
			case 0:
				lua_pop(L,1);
				int u_ref = LUA_NOREF;
				inline_as3(
					"if (o2 in __lua_objrefs) {\n"
					"  %0 = __lua_objrefs[o2];\n"
					"} else {\n"
					"  %0 = -2;\n"
					"}\n" : "=r"(u_ref) : 
				);

				if (u_ref == LUA_NOREF) {
					// Push the new userdata onto the stack
					FlashObj *result = push_newflashref(L);
					
					// Get the prop, and store it with the new key
					inline_as3("__lua_objrefs[%0] = o2;\n" : : "r"(result));
					inline_as3("__lua_objrefs[o2] = %0;\n" : : "r"(result));
				} else { // We already have this object...
					luaL_getsubtable(L, LUA_REGISTRYINDEX, "flash_refs");
					lua_rawgeti(L,-1, u_ref);
					lua_replace(L,-2);
					if (lua_type(L,-1) == LUA_TNIL) { // Got trolled...
						lua_pop(L,1);
						// Push the new userdata onto the stack
						FlashObj *result = push_newflashref(L);
					
						// Get the prop, and store it with the new key
						inline_as3("__lua_objrefs[%0] = o2;\n" : : "r"(result));
						inline_as3("__lua_objrefs[o2] = %0;\n" : : "r"(result));
					}
				}
				int t_ref = LUA_NOREF;
				inline_as3(
					"if (o2.constructor in __lua_typerefs[%1]) {\n"
					"  %0 = int(__lua_typerefs[%1][o2.constructor]);\n"
					"} else {\n" 
					"  %0 = -2;\n"
					"}\n"
					: "=r"(t_ref)
					: "r"(getMainThread(L))
				);
				if (t_ref != LUA_NOREF) {
					lua_rawgeti(L, LUA_REGISTRYINDEX, t_ref);
					lua_pushvalue(L,-2);
					lua_pcall(L, 1, 1, 0);
					lua_replace(L,-2);
				}
				break;
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}


static int flash_safesetprop (lua_State *L) {
	FlashObj *o1 = (FlashObj*) lua_touserdata(L, 1); // Guaranteed to be flash reference, only accessible via metamethod
	size_t l;
	const char *s = luaL_checklstring(L, 2, &l);
	AS3_DeclareVar(propname, String);
	AS3_CopyCStringToVar(propname, s, l);
	AS3_DeclareVar(propVal, Object);
	switch(lua_type(L, 3)) {
		case LUA_TBOOLEAN: inline_as3("propVal = Boolean(%0);\n" : : "r"(lua_toboolean(L, 3))); break;
		case LUA_TNUMBER: inline_as3("propVal = %0;\n" : : "r"(luaL_checknumber(L, 3))); break;
		case LUA_TUSERDATA: 
		{
			void* ud = lua_touserdata(L, 3);
			int isFlash = 0;
			inline_as3("%0 = (int) (%1 in __lua_objrefs);\n" : "=r"(isFlash) : "r"((int) ud));
			if (isFlash) {
				inline_as3("propVal = __lua_objrefs[%0];\n" : : "r"(getObjRef(L, 3)));
				break;
			} else {
				isFlash = luaL_getmetafield(L, 3, "__as3");
				if (isFlash) { // Conversion to AS3 exists.
					lua_pushvalue(L, 3);
					lua_call(L, 1, 1);
					inline_as3("propVal = __lua_objrefs[%0];\n" : : "r"(lua_touserdata(L, -1)));
					lua_pop(L, 1);
				} else { // we convert as a LuaReference
					lua_pushvalue(L, 3);
					int uRef = luaL_ref(L,LUA_REGISTRYINDEX);
					inline_as3("var udRef:* = new LuaReference(%0, %1);\n" : : "r"(L), "r"(uRef));
					inline_as3("propVal = udRef;\n" : : );
				}
			}
			break;
		}
		case LUA_TSTRING:
		{
			const char *s = luaL_checklstring(L, 3, &l);
			AS3_DeclareVar(strvar, String);
			AS3_CopyCStringToVar(strvar, s, l);
			inline_as3("propVal = strvar;\n" : :);
			break;
		}
		case LUA_TFUNCTION:
		case LUA_TTABLE:
		case LUA_TTHREAD:
		{
			int l_ref = luaL_ref(L, LUA_REGISTRYINDEX);
			inline_as3("propVal = new LuaReference(%0,%1);\n" : : "r"(L), "r"(l_ref));
			break;
		}
		default:
			inline_as3("trace(\"unknown: \" + %0);\n" :  : "r"(lua_type(L, 3)));
			return 0;
	}
	inline_as3(
		"try{\n"
		"  __lua_objrefs[%0][propname] = propVal;\n"
		"} catch (e:Error) {\n" // Silence error.
		"} finally {\n"
		"  if (propVal is LuaReference) {(propVal as LuaReference).decRef();}" // Decrement reference count.
		"}"
		: 
		: "r"(o1)
	);
	lua_pop(L, 3);
	return 0; // Sorry what the fuck are we returning?
}

static int toarray_depth = 0;
static const int TOARRAY_MAX_DEPTH = 32; // stop recursion...

static int flash_toarray (lua_State *L) {
	luaL_checktype(L, 1, LUA_TTABLE);
	int len = lua_rawlen(L,1); // Table length.
	inline_as3("var arr:Array = new Array(%0);\n" : : "r"(len));
	size_t l = 0; // string length
	int i;
	for (i = 0; i < len; i++){
		lua_rawgeti(L,1,i+1); // value at 2.
		switch(lua_type(L, 2)) {
			case LUA_TBOOLEAN: inline_as3("arr[%1] = Boolean(%0);\n" : : "r"(lua_toboolean(L, 2)), "r"(i)); break;
			case LUA_TNUMBER: inline_as3("arr[%1] = %0;\n" : : "r"(lua_tonumber(L, 2)), "r"(i)); break;
			case LUA_TTABLE: 
			{
				toarray_depth++;
				if (toarray_depth > TOARRAY_MAX_DEPTH) {
					toarray_depth = 0;
					luaL_error(L,"toarray: Reached maximum recursion depth.");
				}
				lua_pushcfunction(L, flash_toarray);
				lua_pushvalue(L,2);
				lua_call(L, 1, 1);
				inline_as3("arr[%1] = __lua_objrefs[%0];\n" : : "r"((FlashObj*) lua_touserdata(L, -1)), "r"(i));
				toarray_depth--;
				lua_pop(L,1); // Remove userdata off stack
				break;
			}
			case LUA_TFUNCTION:
			case LUA_TTHREAD:
			{
				inline_as3("arr[%0] = null;\n" : : "r"(i));
				break;
			}
			case LUA_TUSERDATA:
			{
				void* ud = lua_touserdata(L, i);
				int isFlash = 0;
				inline_as3("%0 = (int) (%1 in __lua_objrefs);\n" : "=r"(isFlash) : "r"((int) ud));
				if (isFlash) {
					inline_as3("arr[%1] = __lua_objrefs[%0];\n" : : "r"(getObjRef(L, i)), "r"(i));
					break;
				} else {
					isFlash = luaL_getmetafield(L, i, "__as3");
					if (isFlash) { // Conversion to AS3 exists.
						lua_pushvalue(L, i);
						lua_call(L, 1, 1);
						inline_as3("arr[%1] = __lua_objrefs[%0];\n" : : "r"(lua_touserdata(L, -1)), "r"(i));
						lua_pop(L, 1);
					} else { // we do not convert it. sad :(
						inline_as3("arr[%0] = null;\n" : : "r"(i));
					}
				}
				break;
			}
			case LUA_TSTRING:
			{
				const char *s = luaL_checklstring(L, 2, &l);
				AS3_DeclareVar(strvar, String);
				AS3_CopyCStringToVar(strvar, s, l);
				inline_as3("arr[%0] = strvar;\n" : : "r"(i));
				break;
			}
			default:
				inline_as3("trace(\"unknown: \" + %0);\n" :  : "r"(lua_type(L, 2)));
				return 0;
		}
		lua_pop(L,1); // Pop array value from stack
	}
	lua_pop(L,1); // Pop table from stack
	// Push array reference
	FlashObj *result = push_newflashref(L);

	// Get the prop, and store it with the new key
	inline_as3("__lua_objrefs[%0] = arr;\n" : : "r"(result) );
	inline_as3("__lua_objrefs[arr] = %0;\n" : : "r"(result) );
	return 1;
}

static int toobject_depth = 0;
static const int TOOBJECT_MAX_DEPTH = 32; // Arbitrary, but prevents infinite loops in recursion without having to properly check recursion. Nobody needs objects nested this deep anyways.

static int flash_toobject (lua_State *L) {
	luaL_checktype(L, 1, LUA_TTABLE);
	/* table is in the stack at index 't' */
	size_t lKey = 0; // key length
	size_t lVal = 0; // value length, if string.
	inline_as3("var object:Object = new Object();\n" : : );
	AS3_DeclareVar(keyVal, String);
	lua_pushnil(L);  /* first key */
	while (lua_next(L, 1) != 0) {
		/* uses 'key' (at index -2) and 'value' (at index -1) */
		if (lua_type(L, -2) == LUA_TSTRING){
			const char *sKey = lua_tolstring(L, -2, &lKey); // This'll fuck up the stack if we dont check for string first
			AS3_CopyCStringToVar(keyVal, sKey, lKey);
			switch(lua_type(L, -1)) {
				case LUA_TBOOLEAN: inline_as3("object[keyVal] = Boolean(%0);\n" : : "r"(lua_toboolean(L, -1))); break;
				case LUA_TNUMBER: inline_as3("object[keyVal] = %0;\n" : : "r"(luaL_checknumber(L, -1))); break;
				case LUA_TTHREAD: // Tables get converted too, but not these.
				case LUA_TFUNCTION:
				{
					// Just do nothing with these...
					break;
				}
				case LUA_TUSERDATA:
				{
		  		void* ud = lua_touserdata(L,-1);
		  		int isFlash = 0;
			  	inline_as3("%0 = (int) (%1 in __lua_objrefs);\n" : "=r"(isFlash) : "r"((int) ud));
				  if (isFlash) {
				  	inline_as3("object[keyVal] = __lua_objrefs[%0];\n" : : "r"(getObjRef(L, -1)));
						break;
					} else {
						isFlash = luaL_getmetafield(L, -1, "__as3");
						if (isFlash) { // Conversion to AS3 exists.
							lua_pushvalue(L, -1);
							lua_call(L, 1, 1);
							inline_as3("object[keyVal] = __lua_objrefs[%0];\n" : : "r"(lua_touserdata(L, -1)));
							lua_pop(L, 1);
						} // Otherwise, we just do nothing :(
					}
					break;
				}
				case LUA_TTABLE:
				{
					toobject_depth++;
					if (toobject_depth > TOOBJECT_MAX_DEPTH) {
						toobject_depth = 0;
						luaL_error(L,"toobject: Reached maximum recursion depth.");
					}
					lua_pushcfunction(L, flash_toobject);
					lua_pushvalue(L,-2); // value is -2 now, because we pushed function
					lua_call(L, 1, 1);
					inline_as3("object[keyVal] = __lua_objrefs[%0];\n" : : "r"((FlashObj*) lua_touserdata(L, -1)));
					toobject_depth--;
					lua_pop(L,1); // Remove userdata off stack
					break;
				}
				case LUA_TSTRING:
				{
					const char *s = luaL_checklstring(L, -1, &lVal);
					AS3_DeclareVar(strvar, String);
					AS3_CopyCStringToVar(strvar, s, lVal);
					inline_as3("object[keyVal] = strvar;\n");
					break;
				}
				default:
					inline_as3("trace(\"unknown: \" + %0);\n" :  : "r"(lua_type(L, -1)));
					return 0;
			}
		}
		/* removes 'value'; keeps 'key' for next iteration */
		lua_pop(L, 1);
	}
	// Push object reference
	FlashObj *result = push_newflashref(L);
	// Get the prop, and store it with the new key
	inline_as3("__lua_objrefs[%0] = object;\n" : : "r"(result));
	inline_as3("__lua_objrefs[object] = %0;\n" : : "r"(result));
	return 1;
}

static int flash_type (lua_State *L) {
	FlashObj obj = (FlashObj) getObjRef(L, 1);
	char *str = NULL;
	inline_as3("import flash.utils.getQualifiedClassName;\n");
	inline_as3(
		"try{\n"
		"  %0 = CModule.mallocString(getQualifiedClassName(__lua_objrefs[%1]));\n"
		"} catch(e:Error) {}\n"
		: "=r"(str)
		: "r"(obj)
	);
	if (str == NULL) {
		luaL_error(L, "out of memory");
	}
	lua_pushfstring(L, "%s", str);
	return 1;
}

static int flash_register (lua_State *L){
	size_t l;
	const char *s1 = luaL_checklstring(L, 1, &l);
	AS3_DeclareVar(classname, String);
	AS3_CopyCStringToVar(classname, s1, l);
	inline_as3("import flash.utils.getDefinitionByName;\n");
	inline_as3("var clz:Class = getDefinitionByName(classname);\n");
	luaL_checktype(L, 2, LUA_TFUNCTION);
	int ref = luaL_ref(L, LUA_REGISTRYINDEX);
	inline_as3("__lua_typerefs[%0][clz] = %1;\n" : : "r"(L), "r"(ref));
	return 0;
}

LUALIB_API void luaL_registerAS3Conversion(lua_State *L, const char *className) { // Pulls a function off top of stack to use for converting.
	size_t len = strlen(className);
	AS3_DeclareVar(classname, String);
	AS3_CopyCStringToVar(classname, className, len);
	inline_as3("import flash.utils.getDefinitionByName;\n");
	inline_as3("var clz:Class = getDefinitionByName(classname);\n");
	int ref = luaL_ref(L, LUA_REGISTRYINDEX);
	inline_as3("__lua_typerefs[%0][clz] = %1;\n" : : "r"(L), "r"(ref));
}

static int flash_convertArrayToReturns(lua_State *L){
	FlashObj* obj = (FlashObj*) lua_touserdata(L, 1);
	lua_pop(L, 1);
	inline_as3(
  "var arr:LuaResultArray = __lua_objrefs[%0] as LuaResultArray;\n"
	"for (var i:int = 0; i < arr.length; i++) {\n"
	"  pushAS3(%1, arr[i]);\n"
	"}\n"
	:
	: "r"((int) obj), "r"((int) L)
  );
	return lua_gettop(L);
}

static int flash_getclass (lua_State *L){
	size_t l;
	const char *s1 = luaL_checklstring(L, 1, &l);
	AS3_DeclareVar(classname, String);
	AS3_CopyCStringToVar(classname, s1, l);
	inline_as3("import flash.utils.getDefinitionByName;\n");
	inline_as3("var clz:Class = getDefinitionByName(classname);\n");

	int u_ref = LUA_NOREF;
	inline_as3(
		"if (clz in __lua_objrefs) {\n"
		"  %0 = __lua_objrefs[clz];\n"
		"} else {\n"
		"  %0 = -2;\n"
		"}\n" : "=r"(u_ref) : 
	);

	if (u_ref == LUA_NOREF) {
		// Push the new userdata onto the stack
		FlashObj *result = push_newflashref(L);
		
		// Get the prop, and store it with the new key
		inline_as3("__lua_objrefs[%0] = clz;\n" : : "r"(result));
		inline_as3("__lua_objrefs[clz] = %0;\n" : : "r"(result));
	} else { // We already have this object...
		luaL_getsubtable(L, LUA_REGISTRYINDEX, "flash_refs");
		lua_rawgeti(L,-1, u_ref);
		lua_replace(L,-2);
	}
	return 1;
}

static int flash_panic(lua_State *L) { // Replace Lua panic function with AS3 error.
	size_t l;
	const char *errMsg;
	AS3_DeclareVar(luaErr, String);
	if (lua_type(L, -1) == LUA_TSTRING) {
		luaL_checklstring(L, -1, &l);
		AS3_CopyCStringToVar(luaErr, errMsg, l);
		lua_settop(L, 0); // set top to 0.
		inline_as3("throw new Error(\"Lua Panic: \" + luaErr);");
	} else { // lua wont make a string if its out of memory.
		lua_settop(L, 0);
		inline_as3("throw new Error(\"Lua Panic: Out Of Memory\");");
	}
	return 0;
}


static int flash_checkRuffleBug(lua_State *L) {
	unsigned long long v1 = 0xBF58476D1CE4E5B9ULL;
	unsigned long long v2 = 0x94D049BB133111EBULL;
	unsigned long long exp = 0x42D4E4146CC929D3ULL;
	// Trick the compiler :trolling:
	void* pv1 = lua_newuserdata(L, sizeof(unsigned long long));
	void* pv2 = lua_newuserdata(L, sizeof(unsigned long long));
	memcpy(pv1, &v1, sizeof(unsigned long long));
	memcpy(pv2, &v2, sizeof(unsigned long long));
	lua_pushcfunction(L, flash_gettimer);
	lua_call(L, 0, 1);
	lua_pop(L, 1);
	memcpy(&v1, pv1, sizeof(unsigned long long));
	memcpy(&v2, pv2, sizeof(unsigned long long));
	// Done tricking the compiler.
	lua_pushboolean(L, exp != (v1 * v2));
	return 1;
}


// ===============================================================
//                          Registration
// ===============================================================

static const luaL_Reg flashlib[] = {
	{"trace", flash_trace},
	{"gettimer", flash_gettimer},

	/* clean these out, they wont be used and are not meant to be used.
	{"newcallback", flash_newcallback},

	{"getprop", flash_getprop},
	{"getint", flash_getint},
	{"getuint", flash_getuint},
	{"getnumber", flash_getnumber},
	{"getstring", flash_getstring},

	{"setprop", flash_setprop},
	{"setint", flash_setint},
	{"setuint", flash_setuint},
	{"setnumber", flash_setnumber},
	{"setstring", flash_setstring},

	{"getx", flash_getx},
	{"gety", flash_gety},
	{"setx", flash_setx},
	{"sety", flash_sety},

	{"asnumber", flash_asnumber},
	{"asint", flash_asint},
	{"asuint", flash_asuint},
	{"asstring", flash_asstring},

	{"newintvec", flash_newintvec},
	{"newuintvec", flash_newuintvec},
	{"newnumbervec", flash_newnumbervec},

	{"setidxint", flash_setidxint},
	{"setidxuint", flash_setidxuint},
	{"setidxnumber", flash_setidxnumber},
	{"getidxint", flash_getidxint},
	{"getidxuint", flash_getidxuint},
	{"getidxnumber", flash_getidxnumber},
	{"callstatic", flash_callstatic},
	{"hasprop", flash_hasprop},
	{"tolua", flash_tolua},
	*/

	{"new", flash_new},
	//{"safegetprop", flash_safegetprop},
	//{"safesetprop", flash_safesetprop},
	{"toarray", flash_toarray},
	{"toobject", flash_toobject},
	{"type", flash_type},
	{"registerConversion", flash_register},
	{"getclass", flash_getclass},
	
	{"checkRuffleBug", flash_checkRuffleBug},

	{NULL, NULL}
};

LUAMOD_API int luaopen_flash (lua_State *L) {
	luaL_newlib(L, flashlib);

	luaL_newmetatable(L, "flash");
	luaL_setfuncs(L, FlashObj_meta, 0);
	lua_pushliteral(L, "__type");
	lua_pushliteral(L, "flash");
	lua_rawset(L,-3);
	//lua_pushliteral(L, "__index");
	//lua_pushvalue(L, -3);
	//lua_rawset(L, -3);
	//lua_pushliteral(L, "__metatable");
	//lua_pushvalue(L, -3);
	//lua_rawset(L, -3);
	lua_pop(L, 1);

	lua_pushliteral(L, "flash_refs");
	lua_newtable(L);
	lua_newtable(L);
	lua_pushliteral(L, "__mode");
	lua_pushliteral(L, "v");
	lua_settable(L, -3);
	lua_setmetatable(L,-2);
	lua_settable(L,LUA_REGISTRYINDEX);
	
	lua_atpanic(L, flash_panic);

	inline_as3("import flash.utils.Dictionary; __lua_typerefs[%0] = new Dictionary();\n" : : "r"(L));
	
	lua_pushcfunction(L, flash_convertArrayToReturns);
	luaL_registerAS3Conversion(L,"crossbridge.lua.LuaResultArray");
	
	return 1;
}
