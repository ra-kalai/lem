--
-- This file is part of LEM, a Lua Event Machine.
-- Copyright 2011-2013 Emil Renner Berthing
-- Copyright 2012-2013 Asbjørn Sloth Tønnesen
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

local format = string.format

local httpserv = require 'lem.http.server'
local httpresp = require 'lem.http.response'

local M = {}

local urldecode = httpserv.urldecode
M.urldecode = urldecode
M.parseform = httpserv.parseform

function M.debug() end

local Hathaway = {}
Hathaway.__index = Hathaway
M.Hathaway = Hathaway

function Hathaway:GET(path, handler)
	self.lookup[path] = self.lookup[path] or {}
	local entry = self.lookup[path]

	entry.HEAD = handler
	entry.GET  = handler
end

do
	local function static_setter(method)
		return function(self, path, handler)
			self.lookup[path] = self.lookup[path] or {}
			self.lookup[path][method] = handler
		end
	end

	Hathaway.POST    = static_setter('POST')
	Hathaway.PUT     = static_setter('PUT')
	Hathaway.DELETE  = static_setter('DELETE')
	Hathaway.OPTIONS = static_setter('OPTIONS')
end

function Hathaway:GETM(pattern, handler)
	local i = 1
	while true do
		if not self.lookup_m[i] then
			self.lookup_m[i] = {pattern}
			break
		elseif self.lookup_m[i][1] == pattern then
			break
		end
		i = i + 1
	end

	local entry = self.lookup_m[i]

	entry.HEAD = handler
	entry.GET  = handler
end

do
	local function match_setter(method)
		return function(self, pattern, handler)
			local i = 1
			while true do
				if not self.lookup_m[i] then
					self.lookup_m[i] = {pattern}
					break
				elseif self.lookup_m[i][1] == pattern then
					break
				end
				i = i + 1
			end

			local entry = self.lookup_m[i]
			entry[method] = handler
		end
	end

	Hathaway.POSTM    = match_setter('POST')
	Hathaway.PUTM     = match_setter('PUT')
	Hathaway.DELETEM  = match_setter('DELETE')
	Hathaway.OPTIONSM = match_setter('OPTIONS')
end

local function check_match(entry, req, res, ok, ...)
	if not ok then return false end
	local handler = entry[req.method]
	if handler then
		handler(req, res, ok, ...)
		return true
	end
	return false
end

local function handle(self, req, res)
	local method, path = req.method, req.path
	self.debug('info', format("%s %s HTTP/%s", method, req.uri, req.version))
	local lookup = self.lookup
	local entry = lookup[path]

	if entry then
		local handler = entry[method]
		if handler then
			handler(req, res)
		else
			httpresp.method_not_allowed(req, res)
		end

		return 
	end

	lookup = self.lookup_m
	local fine = false

	for i=1, #lookup do
		entry = lookup[i]
		if check_match(entry, req, res, path:match(entry[1])) then
			fine = true
			break
		end
	end

	if not fine then
		httpresp.method_not_allowed(req, res)
	end
end

Hathaway.handle = handle

function Hathaway:run(host, port)
	local conf = {}
	local server, err

	local old_setup = true


	if type(host) == "table" then
		local udataonlyintable = true
		for i, v in pairs(host) do
			if type(v) ~= "userdata" then
				udataonlyintable = false
			end
		end

		if udataonlyintable == false then
			conf = host
			old_setup = false
		end
	end

	if old_setup then
		if port then
			conf.host = host
			conf.port = port
		else
			conf.socket = host
		end
	end


	if conf.socket then
		server, err = httpserv.new(conf.socket, self.handler)
	else
		server, err = httpserv.new(conf.host, conf.port, self.handler)
	end

	if not server then
		self.debug('new', err)
		return nil, err
	end

	self.server = server
	server.debug = self.debug

	local ok, err = server:run(conf.wrapHTTP)
	if not ok and err ~= 'interrupted' then
		self.debug('run', err)
		return nil, err
	end
	return true
end

function Hathaway:import(env)
	if not env then
		env = _G
	end

	env.GET      = function(...) self:GET(...) end
	env.POST     = function(...) self:POST(...) end
	env.PUT      = function(...) self:PUT(...) end
	env.DELETE   = function(...) self:DELETE(...) end
	env.OPTIONS  = function(...) self:OPTIONS(...) end
	env.GETM     = function(...) self:GETM(...) end
	env.POSTM    = function(...) self:POSTM(...) end
	env.PUTM     = function(...) self:PUTM(...) end
	env.DELETEM  = function(...) self:DELETEM(...) end
	env.OPTIONSM = function(...) self:OPTIONSM(...) end
	env.Hathaway = function(...) return self:run(...) end

	return self
end

local function new()
	local self = {
		lookup = {},
		lookup_m = {},
		debug = M.debug
	}
	self.handler = function(...) return handle(self, ...) end
	return setmetatable(self, Hathaway)
end

M.new = new

function M.import(...)
	return new():import(...)
end

return M

-- vim: ts=2 sw=2 noet:
