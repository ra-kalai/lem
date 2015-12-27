/*
 * This file is part of LEM, a Lua Event Machine.
 * Copyright 2011-2013 Emil Renner Berthing
 *
 * LEM is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * LEM is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with LEM.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <lem-parsers.h>

#include <../lem/signal/core.c>
#include <../lem/http/core.c>
#include <../lem/lfs/core.c>
#include <../lem/io/core.c>
#include <../lem/os/core.c>
#include <../lem/parsers/core.c>
#include <../lem/utils/core.c>

static const luaL_Reg lem_loadedlibs[] = { 
	{"lem.utils", luaopen_lem_utils_core},
	{"lem.parsers.core", luaopen_lem_parsers_core},
	{"lem.signal.core", luaopen_lem_signal_core},
	{"lem.http.core", luaopen_lem_http_core},
	{"lem.lfs.core", luaopen_lem_lfs_core},
	{"lem.io.core", luaopen_lem_io_core},
	{"lem.os.core", luaopen_lem_os_core},
	{NULL, NULL}
};

#if LUA_VERSION_NUM == 501
void luaL_requiref(lua_State *L, const char *modname, lua_CFunction openf, int glb) {
	luaL_checkstack(L, 3, "not enough stack slots available");
	lua_pushstring(L, "_LOADED");
	lua_gettable(L, LUA_REGISTRYINDEX);
	lua_getfield(L, -1, modname);
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
		lua_pushcfunction(L, openf);
		lua_pushstring(L, modname);
		lua_call(L, 1, 1);
		lua_pushvalue(L, -1);
		lua_setfield(L, -3, modname);
	}
	if (glb) {
		lua_pushvalue(L, -1);
		lua_setglobal(L, modname);
	}
	lua_replace(L, -2);
}
#endif


static void load_lem_libs(lua_State *L) {
	const luaL_Reg *lib;
	for (lib = lem_loadedlibs; lib->func; lib++) {
		luaL_requiref(L, lib->name, lib->func, 1); 
		lua_pop(L, 1);  /* remove lib */
	}
}
