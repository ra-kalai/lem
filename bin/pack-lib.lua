#!bin/local-lem
--
-- This file is part of LEM, a Lua Event Machine.
-- Copyright 2011-2012 Emil Renner Berthing
--
-- LEM is free software: you can redistribute it and/or modify it
-- under the terms of the GNU Lesser General Public License as
-- published by the Free Software Foundation, either version 3 of
-- the License, or (at your option) any later version.
--
-- LEM is distributed in the hope that it will be useful, but
-- WITHOUT ANY WARRANTY; without even the implied warranty of
-- MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
-- GNU Lesser General Public License for more details.
--
-- You should have received a copy of the GNU Lesser General Public
-- License along with LEM.  If not, see <http://www.gnu.org/licenses/>.
--


--
-- This lua script is used during the build of a static lem-interpreter or
-- self contained lua script
--
-- arg[1] => C file containing all lua script
-- arg[2] => C header filer which include all C module,
--

package.path = '?.lua'
package.cpath = '?.so;?.dll'

local g_debug = os.getenv('V')

local lfs = require 'lem.lfs'
local io = require 'lem.io'
local glob = lfs.glob

local serialize   -- alias of the function to serialize lua code
local loadbc      -- alias of the function to load bytecode / lua string
local lstring     -- the name of the function to load bytecode / lua string on the current __VERSION of lua

local format = string.format


if _VERSION == 'Lua 5.1' then
	serialize = string.dump
	lstring = 'loadstring'
	loadbc = loadstring
else
	if g_debug then
		serialize = function (f) return string.dump(f) end
	else
		serialize = function (f) return string.dump(f, true) end
	end
	lstring = 'load'
	loadbc = load
end

local lualib ={}

lualib[#lualib+1] = [==[
(function ()
	local lstring = ]==] .. lstring .. [==[

	local g_require = {
]==]

local core_lua_file_list = {'lem/*/*.lua', 'lem/*.lua'}

local cmd = nil
local extra_lua_file = {}
local extra_c_files = {}
local extra_object = {}
local extra_luaopen = {}

local lem_extra_pack = os.getenv('LEM_EXTRA_PACK') or ''

local basepath
for manifest in lem_extra_pack:gmatch("([^:]+):?") do
	basepath = manifest:gsub("[^/]*$", '')
	local manifest = loadfile(manifest)()

	if manifest.lua_files then
		for i, v in pairs(manifest.lua_files) do
			if v[1] == 'main' then
				extra_lua_file[#extra_lua_file + 1] = {v[2], basepath .. v[2] }
				cmd = v[2]:gsub('.lua$', '')
			elseif v[1] == 'extra' then
				extra_lua_file[#extra_lua_file + 1] = {v[2], basepath .. v[2] }
			end
		end
	end

	if manifest.c_files then
		for i, v in pairs(manifest.c_files) do
			if v[1] == 'extra' then
				extra_c_files[#extra_c_files + 1] = {v[2], basepath .. v[2] }
			end
		end
	end

	if manifest.object_files then
		for i, v in pairs(manifest.object_files) do
			if v[1] == '.o' then
				extra_object[#extra_object + 1] = {v[2], basepath .. v[2] }
			end
		end
	end

	if manifest.luaopen then
		for i, v in pairs(manifest.luaopen) do
			extra_luaopen[#extra_luaopen + 1] = v
		end
	end
end

local c

for i, path in pairs(core_lua_file_list) do
	for i, v in pairs(glob(path)) do
		c = io.open(v):read("*a")
		local lpath = v:gsub("^%./",''):gsub('%.lua$',''):gsub('/','.')
		if g_debug then
			io.stderr:write(
				format('packing %-30s -> %s\n', v , lpath))
		end
		lualib[#lualib+1] = format("[%q]={lstring(%q)},",
			lpath,
			serialize(loadbc(c)))
	end
end

for i, v in pairs(extra_lua_file) do
	c = io.open(v[2]):read("*a")
	c = c:gsub('^#![^\n]*\n(.*)', '%1')
	c, err = loadbc(c)
	if err then
		print(err)
	end
	local lpath = v[1]:gsub("^%./",''):gsub('.lua$',''):gsub('/','.')
	if g_debug then
		io.stderr:write(
			format('packing %-30s -> %s\n', v[2] , lpath))
	end
	lualib[#lualib+1] = format("[%q]={lstring(%q)},",lpath, serialize(c))
end

lualib[#lualib+1] = [==[
	}

	local old_require = require
	require = function (modname)
		local f = g_require[modname]

		if f then
			if f[1] then
				f[2] = f[1]()
				f[1] = false
				return f[2]
			else
				return f[2]
			end
		end

		return old_require(modname)
	end
end)()
]==]

if cmd then
	lualib[#lualib+1] = format('command_chain = {%q, function() require %q end}', cmd, cmd)
end

-- embed this Lua script in a C file
local c = 0
local out = format([[
#include <stdint.h>
const char lem_lualib_preamble[] = {
%s]],
	serialize(loadbc(table.concat(lualib, '')))
	:gsub(".", function (m)
		c = c + 1
		if c % 20 == 0 then
			return '0x' .. format('%02x,\n', string.byte(m))
		else
			return '0x' .. format('%02x,', string.byte(m))
		end
	end) ..
'};')

local out_file = io.open(arg[1], "w")
out_file:write(out)
out_file:close()

out = [[
#ifndef _STATIC_CLIB_EXTRA_INCLUDE_
#define _STATIC_CLIB_EXTRA_INCLUDE_

]]
..
(function ()
	local o = {}

	for i, v in pairs(extra_c_files) do
		o[#o + 1] = format('#include "../%s"\n', v[2])
	end

	for i, v in pairs(extra_luaopen) do
		o[#o + 1] = format('extern int %s(lua_State *);\n', v[2])
	end

	return table.concat(o)
end)()
..
[[


#else
/* on 2nd include, preload these libraries */

]]
..
(function ()
	local o = {}

	for i, v in pairs(extra_c_files) do
		o[#o + 1] = format('{"%s", %s},', v[1]:gsub(".[^.]*$", "")
																					:gsub("/","."),
																			'luaopen_' ..
																			(v[1]:gsub(".[^.]*$", "")
																					:gsub("/","_"))

																			)
	end

	for i, v in pairs(extra_luaopen) do
		o[#o + 1] = format('{"%s", %s},', v[1], v[2])
	end

	return table.concat(o)
end)()
..
[=[

#endif
]=]

out_file = io.open(arg[2], "w")
out_file:write(out)
out_file:close()

out_file = io.open(arg[3], "w")

;

(function ()
	local o = {}
	for i, v in pairs(extra_object) do
		o[#o + 1] = format('%s ', v[2])
	end
	out = table.concat(o)
	print(out)
end)()

out_file:write(out)
out_file:close()

-- vim: syntax=lua ts=2 sw=2 noet:
