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
package.cpath = '?.so;?.dll'

local utils = require 'lem.utils'
local io    = require 'lem.io'
local os    = require 'lem.os'

local write, format = io.write, string.format

function bg()
	local p, pid = io.popen('sleep 1; sleep 1; exit 2', 'rw')
	
	-- block the current coroutine until, a child process finish
	print(utils.thisthread(), os.date())
	local status = os.waitpid(pid, 0)
	print(utils.thisthread(), os.date())
	for i, v in pairs(status) do
		print(i, v)
	end
end

utils.spawn(bg)
utils.spawn(bg)


local p, pid = io.popen('sleep 1', 'rw')

-- block the current coroutine and popen finish
local status = os.waitpid(pid, 0)
for i, v in pairs(status) do
	print(i, v)
end


-- make a child process who should die of a broken pipe
(function ()
	local _
	_, pid = io.popen("sleep 1 ; echo 1", "rw")
end)()

os.execute("sleep 2")

-- block the current coroutine until child return finish
local options = os.waitpid_options.WCONTINUED | os.waitpid_options.WUNTRACED
local status, errmsg = os.waitpid(-1, options)
print('sometime ok sometime nok why ? weird')
if status then
	for i, v in pairs(status) do
		print(i, v)
	end
else
	print(status, errmsg)
end


local options = os.waitpid_options.WNOHANG


print(os.waitpid(1, 0))

-- vim: syntax=lua ts=2 sw=2 noet:
