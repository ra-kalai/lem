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

local setmetatable = setmetatable
local tonumber = tonumber
local concat = table.concat
local format = string.format

local io    = require 'lem.io'
local http  = require 'lem.http'

local M = {}

local Response = {}
Response.__index = Response
M.Response = Response

function Response:body_chunked()
	if self._body then return self._body end

	local conn = self.conn
	local rope, i = {}, 0
	local line, err
	while true do
		line, err = conn:read('*l')
		if not line then return nil, err end

		local len = tonumber(line, 16)
		if not len then return nil, 'expectation failed' end
		if len == 0 then break end

		local data, err = conn:read(len)
		if not data then return nil, err end

		i = i + 1
		rope[i] = data

		line, err = conn:read('*l')
		if not line then return nil, err end
	end

	line, err = conn:read('*l')
	if not line then return nil, err end

	rope = concat(rope)
	self._body = rope
	return rope
end

function Response:body()
	if self._body then return self._body end
	if self.headers['transfer-encoding'] == 'chunked' then
		return self:body_chunked()
	end

	local len, body, err = self.headers['content-length']
	if len then
		len = tonumber(len)
		if not len then return nil, 'invalid content length' end
		body, err = self.conn:read(len)
	else
		if self.headers['connection'] == 'close' then
			body, err = self.client:read('*a')
		else
			return nil, 'no content length specified'
		end
	end
	if not body then return nil, err end

	self._body = body
	return body
end

local Client = {}
Client.__index = Client
M.Client = Client

function M.new()
	return setmetatable({
		proto = false,
		domain = false,
		conn = false,
	}, Client)
end

local req_method = "%s %s HTTP/1.1\r\n"
local req_method_and_host = "%s %s HTTP/1.1\r\nHost: %s\r\n"

local function close(self)
	local c = self.conn
	if c then
		self.conn = false
		return c:close()
	end
	return true
end
Client.close = close

local function fail(self, err)
	self.proto = false
	close(self)
	return nil, err
end

function Client:request(request)
	if type(request) ~= 'table' then
		error('arg should be a table', 2)
	end

	local header_list = http.new_header_list(request.header_list or {})
	local headers = request.headers or {}


	local method = request.method
	if method == nil then error('missing method', 2) end

	local url = request.url
	if url == nil then error('missing url', 2) end

	local proto, domain_and_port, path = url:match('([a-zA-Z0-9]+)://([^/]+)(/.*)')

	if proto == nil then error('missing protocol in url', 2) end
	if domain_and_port == nil then error('missing domain in url', 2) end
	if path == nil then error('missing path in url', 2) end

	local payload = request.payload or ''

	local proxy_connect_domain
	local proxy_connect_port

	local http_proxy = request.http_proxy
	if http_proxy then
		if proto == 'http' then
			proxy_connect_domain, proxy_connect_port = http_proxy:match("([^:]*):(.*)")
			path = url
		end
	end

	local https_proxy = request.https_proxy
	local use_https_proxy = false
	if https_proxy then
		if proto == 'https' then
			proxy_connect_domain, proxy_connect_port = https_proxy:match("([^:]*):(.*)")
			use_https_proxy = true
		end
	end

	-- merge key values from header map
	for k, v in pairs(headers) do
		header_list:set(k, v)
	end

	local host_is_set = header_list:value('host')

	local rope = {}
	local rope_c = 1

	if host_is_set then
		rope[rope_c] = req_method:format(method, path)
	else
		rope[rope_c] = req_method_and_host:format(method, path, domain_and_port)
	end

	rope_c = rope_c + 1
	rope[rope_c] = header_list:toString()

	rope_c = rope_c + 1
	rope[rope_c] = payload

	local req = concat(rope)

	local res
	local err
	local c

	c = self.conn

	if proto == self.proto and
		domain_and_port == self.domain_and_port then

		-- 2nd request in case connection is keepalive,
		-- and we didn't timeout..

		if c:write(req) then
			res, err = c:read('HTTPResponse')
			if not res then return fail(self, err) end
		end

		res.header_list = http.new_header_list(res.header_list)
		res.headers = res.header_list:toMap()

		res.conn = c

		return setmetatable(res, Response)
	end

	-- it is the first http request / domain changed
	if c then
		c:close()
	end

	local domain
	local specified_port = domain_and_port:match(':([0-9]+)')
	local connect_domain
	local connect_port = specified_port

	if specified_port then
		domain = domain_and_port:gsub(':.*$', '')
	else
		domain = domain_and_port
	end

	connect_domain = domain

	if proxy_connect_domain then
		connect_domain = proxy_connect_domain
	end

	if proxy_connect_port then
		connect_port = proxy_connect_port
	end

	if use_https_proxy == true then
		c, err = io.tcp.connect(connect_domain, connect_port)
		if not c then return fail(self, err) end

		local proxy_connect_domain_and_port
		if specified_port == nil then
			proxy_connect_domain_and_port = domain_and_port .. ':443'
		else
			proxy_connect_domain_and_port = domain_and_port
		end

		c:write(format("CONNECT %s HTTP/1.1\r\nHost: %s\r\nProxy-Connection: Keep-Alive\r\n\r\n",
			proxy_connect_domain_and_port, proxy_connect_domain_and_port))
		local l, err = c:read("*l")
		if err then return fail(self, err) end
		if l:match("200") then
			repeat
				l, err = c:read("*l")
			until l == "\r" or err
			if err then return fail(self, err) end

			local ssl = self.ssl
			if not ssl then
				error('No ssl context defined', 2)
			end
			c, err = ssl:ssl_wrap_socket(c)
		end
	end

	if proto == 'http' then
		c, err = io.tcp.connect(connect_domain, connect_port or '80')
	elseif proto == 'https' then
		if use_https_proxy == false then
			local ssl = self.ssl
			if not ssl then
				error('No ssl context defined', 2)
			end
			c, err = ssl:connect(domain, specified_port or '443')
		end
	else
		error('Unknown protocol', 2)
	end
	if not c then return fail(self, err) end

	local ok
	ok, err = c:write(req)
	if not ok then return fail(self, err) end

	res, err = c:read('HTTPResponse')
	if not res then return fail(self, err) end

	res.header_list = http.new_header_list(res.header_list)
	res.headers = res.header_list:toMap()

	res.conn = c

	self.proto = proto
	self.domain_and_port = domain_and_port
	self.conn = c

	return setmetatable(res, Response)
end

function Client:get(url, extra_headers)
	return self:request({
		method="GET",
		url=url,
		headers=extra_headers,
	})
end

function Client:download(url, filename)
	local res, err = self:get(url)
	if not res then return res, err end

	local file
	file, err = io.open(filename, 'w')
	if not file then return file, err end

	local ok
	ok, err = file:write(res.body)
	if not ok then return ok, err end
	ok, err = file:close()
	if not ok then return ok, err end

	return true
end

return M

-- vim: set ts=2 sw=2 noet:
