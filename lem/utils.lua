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

return lem_utils

-- vim: ts=2 sw=2 noet:
