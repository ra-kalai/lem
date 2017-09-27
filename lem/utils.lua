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
local resume = lem_utils.resume
local thisthread = lem_utils.thisthread

local g_waittid_map = {}

local coroutine_resume = coroutine.resume

local function spawn2(f, ...)
	local tid
	spawn(function (...)
		tid = thisthread()
		yield()
		f(...)
		local tid_resume = g_waittid_map[tid]
		if tid_resume then
			g_waittid_map[tid] = nil
			coroutine_resume(tid_resume, tid)
		end
	end, ...)

	return tid
end

lem_utils.spawn2 = spawn2

local function waittid(tid_list)
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

lem_utils.waittid = waittid

local coro_yield = coroutine.yield
local table_unpack

if _VERSION == 'Lua 5.1' then
	table_unpack = unpack
else
	table_unpack = table.unpack
end

local thread_queue = {
	run = function(self)

		if self.running == false then
			local t = self.tidlist[self.cursor]

			self.running = true
			if t then
				resume(t)
			else
				self.running = false
				self.cursor = 1
				self.tidlist = {}
			end
		end
	end,
	append = function (self, fun)
		self.tidlist = self.tidlist or {}
		local ret
		local tid = spawn2(function ()
			self.tidlist[#self.tidlist+1] = thisthread()
			if self.running == true then
				coro_yield()
			end
			self.running = true
			ret = {fun()}

			self.running = false
		  self.cursor = self.cursor + 1
			self:run()
		end)


		waittid({tid})

		return table_unpack(ret)
	end
}

local thread_queue_mt = { __index = thread_queue }

local new_thread_queue = function ()
	local o = {running = false, cursor=1}
	setmetatable(o, thread_queue_mt)
	return o
end

lem_utils.new_thread_queue = new_thread_queue

lem_utils.usleep = function (t)
	lem_utils.sleep(t/1000.)
end

lem_utils.sleep = function (t)
	local sleeper = lem_utils.newsleeper(t)
	sleeper:sleep(t)
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

	local szstr = lem_utils.szstr

	local function szfun(f)
		local ret = {
		 '(function ()',
		 'local n = {}',
		 'local f = ',
		 loadstring_fun,
		 '(',
		 szstr(string_dump(f)),
		 ')',
		 'local t = {',
		}

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
		else return table.concat(code, "\n") end
	end

	lem_utils.serialize = serialize
	lem_utils.unserialize = function (s)
		return loadstring(s)()
	end
end

return lem_utils

-- vim: ts=2 sw=2 noet:
