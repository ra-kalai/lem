#!bin/lem
--
-- This file is part of LEM, a Lua Event Machine.
-- Copyright 2013 Emil Renner Berthing
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

package.path = '?.lua'
package.cpath = '?.so'

local utils = require 'lem.utils'
local io    = require 'lem.io'
local lfs   = require 'lem.lfs'
local utils   = require 'lem.utils'


if arg[1] == 'srv' then
	lfs.remove('socket')
	local sock = assert(io.unix.listen('socket', 666))

	sock:autospawn(function (client)
		print('send stdout and stderr fd to client:', client)
		local t = {}

		-- can pass a table of stream or fd

		for i=1,100,2 do
			t[#t+1] = io.stderr
			t[#t+2] = 1 -- stdout
		end

		local count = io.unix.passfd_send(client,t)
		print(count)
		local count = io.unix.passfd_send(client,t)
		print(count)
	end)
else
	local sock = assert(io.unix.connect('socket'))

	local fdlist, errmsg = io.unix.passfd_recv(sock)
	for i, v in pairs(fdlist) do
		local s = io.fromfd(v)
		s:write('coin ' .. os.time() .. '\n')
	end
	local fdlist, errmsg = io.unix.passfd_recv(sock)
	for i, v in pairs(fdlist) do
		local s = io.fromfd(v)
		s:write('coin ' .. os.time() .. '\n')
	end
end

-- vim: syntax=lua ts=2 sw=2 noet:
