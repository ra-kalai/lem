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

local http    = require 'lem.http.core'
local parsers = require 'lem.parsers'

parsers.lookup['HTTPRequest'] = http.HTTPRequest
http.HTTPRequest = nil
parsers.lookup['HTTPResponse'] = http.HTTPResponse
http.HTTPResponse = nil

local header_list_mt = {}
header_list_mt.__index = header_list_mt

function header_list_mt:toMap()
	local t = {}
	local v
	for i = 1, #self do
		v = self[i]
		t[v[1]:lower()] = v[2]
	end

	return t
end

function header_list_mt:value(key)
	local v
	local lowerkey = key:lower(key)

	for i = 1, #self do
		v = self[i]
		if v[1]:lower(key) == lowerkey then
			return v[2]
		end
	end

	return nil
end

function header_list_mt:unset(key)
	local v
	local set = 0
	local lowerkey = key:lower()

	for i = 1, #self do
		v = self[i]
		if v[1]:lower(key) == lowerkey then
			v[2] = nil
		end
	end
end

function header_list_mt:set(key, value)
	local v
	local set = 0
	local lowerkey = key:lower()

	for i = 1, #self do
		v = self[i]
		if v[1]:lower(key) == lowerkey then
			v[2] = value
			set = set + 1
		end
	end

	if set == 0 then
		self[#self+1] = {key, value}
	end

	return set
end

local concat = table.concat

function header_list_mt:toString()
	local rope = {}
	local rope_i = 1
	local v


	for i = 1, #self do
		v = self[i]
		if v[2] then
			rope[rope_i] = v[1] 	rope_i = rope_i + 1
			rope[rope_i] = ':'  	rope_i = rope_i + 1
			rope[rope_i] = v[2] 	rope_i = rope_i + 1
			rope[rope_i] = "\r\n"	rope_i = rope_i + 1
		end
	end
	rope[rope_i] = "\r\n"

	return concat(rope)
end

function new_header_list(t)
	return setmetatable(t, header_list_mt)
end

http.new_header_list = new_header_list

return http

-- vim: ts=2 sw=2 noet:
