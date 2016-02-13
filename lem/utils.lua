--
-- This file is part of LEM, a Lua Event Machine.
-- Copyright 2011-2013 Emil Renner Berthing
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

local lem_utils  = require 'lem.utils.core'

local spawn = lem_utils.spawn
local yield = lem_utils.yield
local suspend = lem_utils.suspend
local thisthread = lem_utils.thisthread

local g_waittid_map = {}

lem_utils.spawn2 = function (f, ...)
	local tid
	spawn(function (...)
		tid = thisthread()
		yield()
		f(...)
		local tid_resume = g_waittid_map[tid]
		if tid_resume then
			g_waittid_map[tid] = nil
			coroutine.resume(tid_resume, tid)
		end
	end, ...)

	yield()

	return tid
end

lem_utils.waittid = function (tid_list)
	local tid_count = #tid_list
	local current_tid = thisthread()
	local i = 0
	local co

	for i = 1, #tid_list do
		co = tid_list[i]
		if coroutine.status(co) ~= 'dead' then
			g_waittid_map[co] = current_tid
		else
			tid_count = tid_count - 1
		end
	end

	while i < tid_count do
		local tid = suspend()
		i = i + 1
	end

	return tid_count
end

do
	-- Tony serializer
	-- http://lua-users.org/lists/lua-l/2009-11/msg00533.html
	local szt = {}
	local format = string.format

	local loadstring_fun

	local string_dump = function (f)
		return string.dump(f, true)
	end

	if _VERSION == 'Lua 5.1' then
		loadstring_fun = "loadstring"
	else
		loadstring_fun = "load"
		loadstring = load
	end

	local tr_c_map = {
		["\a"]= "\\a",
		["\b"]= "\\b",
		["\f"]= "\\f",
		["\n"]= "\\n",
		["\r"]= "\\r",
		["\t"]= "\\t",
		["\v"]= "\\v",
		["\""]= "\\\"",
		["\\"]= "\\\\",
	}

	local function char(c)
		local r = tr_c_map[c]
		if r then return r end

		return format("\\%03d", c:byte())
	end

	local function szstr(s)
		return
		format('"%s"',
			s :gsub("[^0-9a-zA-Z\t %._-@/()%#!{|}~:;<=>?$&*+`%]%[]", char))
				:gsub("\\([0-9][0-9][0-9])([^0-9])",function (a, b)
					return '\\'..tonumber(a)..b
				end)
	end

	local function szfun(f)
		local ret = {}
		ret[#ret+1] = '(function ()'
		ret[#ret+1] = 'local n = {}'
		ret[#ret+1] = 'local f = '
		ret[#ret+1] = loadstring_fun
		ret[#ret+1] = '('
		ret[#ret+1] = szstr(string_dump(f))
		ret[#ret+1] = ')'
		ret[#ret+1] = 'local t = {'

		local i = 1

		while true do
			local upval, val = debug.getupvalue(f, i)

			if upval == nil then break end

			if upval ~= '_ENV' then
				if type(val) == 'function' then
					ret[#ret + 1] =  loadstring_fun .. '(' .. szstr(serialize(val)) .. ')()'
					elseif type(val) == "nil" then
						ret[#ret+1] = 'nil'
					elseif type(val) == "table" then
						ret[#ret + 1] = loadstring_fun .. '(' .. szstr(serialize(val)) .. ')()'
					else
						ret[#ret + 1] = szany(val)
					end
				else
					ret[#ret + 1] = '_ENV '
				end
			ret[#ret + 1] = ','
			i = i + 1
		end
		ret[#ret + 1] = '}'
		ret[#ret + 1] = 'local i '
		ret[#ret + 1] = 'for i=1, #t do'
		ret[#ret + 1] = '	debug.setupvalue(f, i, t[i])'
		ret[#ret + 1] = 'end '
		ret[#ret+1] = 'return f '
		ret[#ret+1] = 'end)()'

		return table.concat(ret)
	end

	function szany(...) return szt[type(...)](...) end

	local function sztbl(t,code,var)
		for k,v in pairs(t) do
			local ks = szany(k,code,var)
			local vs = szany(v,code,var)
			code[#code+1] = format("%s[%s]=%s", var[t], ks, vs)
		end
		return "{}"
	end

	local function memo(sz)
		return function(d,code,var)
			if var[d] == nil then
				var[1] = var[1] + 1
				var[d] = format("_[%d]", var[1])
				local index = #code+1
				code[index] = "" -- reserve place during recursion
				code[index] = format("%s=%s", var[d], sz(d,code,var))
			end
			return var[d]
		end
	end

	szt["nil"]      = tostring
	szt["boolean"]  = tostring
	szt["number"]   = tostring
	szt["string"]   = szstr
	szt["function"] = memo(szfun)
	szt["table"]    = memo(sztbl)

	function serialize(d)
		local code = { "local _ = {}" }
		local value = szany(d, code, {0})
		code[#code+1] = "return "..value
		if #code == 2 then return code[2]
		else return table.concat(code, "\n")
		end
	end

	lem_utils.serialize = serialize
	lem_utils.unserialize = function (s)
		return loadstring(s)()
	end
end

return lem_utils

-- vim: ts=2 sw=2 noet:
