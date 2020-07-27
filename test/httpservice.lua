#!bin/lem
--
-- This file is part of LEM, a Lua Event Machine.
-- Copyright 2011-2012 Emil Renner Berthing
-- Copyright 2013 Asbjørn Sloth Tønnesen
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

local utils    = require 'lem.utils'
local io       = require 'lem.io'
local HttpService = require 'lem.httpservice'
local BindMethod = HttpService.BindMethod

local server = HttpService {
	BindMethod { "GET",
		{
			match="hello",
			cb=function (req, res)
				return "Hello, World\n"
			end
		},
		{
			match="self",
			cb=function (req, res)
				return arg[0]
			end,
			dcb = HttpService.deliverFile
		},
		{
			match="test2",
			cb=function (req, res)
				return {1,2,3,4}
			end,
			dcb = HttpService.deliverJSON
		},
		{
			match="(.*)",
			cb=function (req, res, args)
				return args[1]
			end,
			dcb = HttpService.deliverFile
		},
	},
	BindMethod { "POST",
		{
			match="hello",
			cb=function (req, res)
	for k, v in pairs(req:formData()) do
		res:add("  ['%s'] = '%s'\n", k, v)
	end
				print("post->"..req:body())
				return "Hello, World\n"
			end
		},
		{
			match="hellojson",
			cb=function (req, res)
				local json=req:jsonData()
				print(json)
	if json then
	for k, v in pairs(json) do
		res:add("  ['%s'] = '%s'\n", k, v)
	end
	end
				print("postjson->"..req:body())
				return "Hello, World\n"
			end
		},
	},
}

server.debug = function (kind, msg) end

server:serve({host="*", port=8080})

utils.exit(0)

-- vim: syntax=lua ts=2 sw=2 noet:
