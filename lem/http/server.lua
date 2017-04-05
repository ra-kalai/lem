--
-- This file is part of LEM, a Lua Event Machine.
-- Copyright 2011-2013 Emil Renner Berthing
-- Copyright 2013      Halfdan Mouritzen
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

local setmetatable = setmetatable
local tonumber = tonumber
local pairs = pairs
local type = type
local concat = table.concat

local io       = require 'lem.io'
local http     = require 'lem.http'
local response = require 'lem.http.response'
local utils = require 'lem.utils'
local response_status_string = response.status_string

local utc_date
do
	local floor = math.floor
	local date = os.date
	local utcd
	local ct
	local now = utils.now

	utc_date = function()
		local t = floor(now())
		if ct ~= t then
			utcd = date('!%a, %d %b %Y %H:%M:%S GMT')
			ct = t
		end

		return utcd
	end
end

local M = {}

function M.debug() end

local Request = {}
Request.__index = Request
M.Request = Request

function Request:body()
	local len, body = self.headers['content-length'], ''
	if not len then return body end

	len = tonumber(len)
	if len <= 0 then return body end

	if self.headers['expect'] == '100-continue' then
		local ok, err = self.client:write('HTTP/1.1 100 Continue\r\n\r\n')
		if not ok then return nil, err end
	end

	local err
	body, err = self.client:read(len)
	if not body then return nil, err end

	return body
end

local urldecode = http.urldecode
M.urldecode = urldecode

local newresponse = response.new

local function handleHTTP(self, client)
	local res

	local keep_serving = true

	repeat
		local req, err = client:read('HTTPRequest')
		if not req then self.debug('read', err) break end

		req.header_list = http.new_header_list(req.header_list)
		req.headers = req.header_list:toMap()

		local method, version = req.method, req.version

		setmetatable(req, Request)
		req.client = client

		res = newresponse(req)

		if version ~= '1.0' and version ~= '1.1' then
			response.version_not_supported(req, res)
			version = '1.1'
		else
			if version == '1.0' then keep_serving = false end

			local expect = req.headers['expect']
			if expect and expect ~= '100-continue' then
				response.expectation_failed(req, res)
			else
				if req.path then
					self.handler(req, res)
					if res.abort == true or res.detach == true then
						break
					end
				else
					response.bad_request(req, res)
				end
			end
		end

		local headers = res.headers
		local file, close_file = res.file, false

		if file then
			if type(file) == 'string' then
				file, err = io.open(file)

				if file then
					close_file = true
				else
					self.debug('open', err)
					res = newresponse(req)
					response.not_found(req, res)
				end
			end
		end

		if res.status == nil then
			if #res == 0 and file == nil then
				res.status = 204
			else
				res.status = 200
			end
		end


		local rope = {
			'HTTP/' .. version .. ' ' .. response_status_string[res.status] .. '\r\n'
		}

		local rope_idx = 2
		if headers['Date'] == nil then
			rope[rope_idx] = 'Date: ' .. utc_date() .. '\r\n'
			rope_idx = rope_idx + 1
		end

		if headers['Server'] == nil then
			rope[rope_idx] = 'Server: Hathaway/0.1 LEM/0.3\r\n'
			rope_idx = rope_idx + 1
		end

		if req.headers['connection'] == 'close' and
			headers['Connection'] == nil then
			rope[rope_idx] = 'Connection: close\r\n'
			rope_idx = rope_idx + 1
			keep_serving = false
		end

		local body
		local body_len = 0
		if headers['Content-Length'] == nil and res.status ~= 204 then
			if file then
				body_len = file:size()
			else
				body = concat(res)
				body_len = #body
			end

			rope[#rope+1] = 'Content-Length: '.. body_len .. '\r\n'
		end

		res:appendheader(rope)

		client:cork()

		local ok, err = client:write(concat(rope))
		if not ok then self.debug('write', err) break end

		if method ~= 'HEAD' then
			if file then
				ok, err = client:sendfile(file, body_len)
				if close_file then file:close() end
			else
				if body_len > 0 then
					ok, err = client:write(body)
				end
			end
			if not ok then self.debug('write', err) break end
		end

		client:uncork()

	until keep_serving ~= true

	if not res.detach then
		client:close()
	end
end

local Server = {}
Server.__index = Server
M.Server = Server

function Server:run(wrapHTTP)
	if wrapHTTP == nil then
		return self.socket:autospawn(function(client) handleHTTP(self, client) end)
	else
		return self.socket:autospawn(function(client) wrapHTTP(handleHTTP, self, client) end)
	end
end

function Server:close()
	return self.socket:close()
end

local type, setmetatable = type, setmetatable

function M.new(host, port, handler)
	local socket, err
	if type(host) == 'string' then
		socket, err = io.tcp.listen(host, port)
		if not socket then
			return nil, err
		end
	else
		socket = host
		handler = port
	end

	return setmetatable({
		socket = socket,
		handler = handler,
		debug = M.debug
	}, Server)
end

return M

-- vim: ts=2 sw=2 noet:
