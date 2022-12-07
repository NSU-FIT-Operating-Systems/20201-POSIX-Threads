local timer = require("timer")
local uv = require("uv")
local core = require("core")

local GThread = core.Object:extend()

function GThread:initialize(fn)
	assert(type(fn) == "function")

	self.fn = fn
	self.coro = coroutine.create(fn)
	self.isThread = true
	self.joinedBy = nil -- = GThread, optional

	self.finalRet = nil
end

function GThread:getCoro() return self.coro end

function GThread:join(t2) -- self waits until t2
	-- assert(type(t2) == "table" and t2.isThread)
	-- assert(not t2.joinedBy, "trying to join self to an already joined thread")
	-- assert(not self.joinedBy, "joining self which is already joined")

	if t2:status() == "dead" then
		return t2.finalRet
	end

	t2.joinedBy = self
	return self:yield()
end

function GThread:run(...)
	return self:resume(self, ...)
end

function GThread:yield(...) return coroutine.yield(self.coro, ...) end
function GThread:status() return coroutine.status(self.coro) end

function GThread:resume(...)
	-- assert(coroutine.status(self.coro) ~= "dead", "can't resume dead GThread")

	local ok, a = coroutine.resume(self.coro, ...)
	if not ok then error(a) end

	if coroutine.status(self.coro) == "dead" and self.joinedBy then
		self.finalRet = a
		self.joinedBy:resume(a)
	end

	return a
end


local threads = {}
local created = 0

local gthreadPairs = 50000

for i=1, gthreadPairs do
	created = created + 1

	local thread1 = GThread:new(function(self)
		timer.setTimeout(math.random(10, 500), self.resume, self)
		self:yield()
	end)

	local thread2 = GThread:new(function(self)
		self:join(thread1)

		created = created - 1
		if created % math.floor(gthreadPairs / 10) == 0 then
			print(created, "thread2's left to complete")
		end
	end)

	table.insert(threads, thread1)
	table.insert(threads, thread2)
end

-- Fisher–Yates shuffle
local function shuffle(t)
	local n = #t

	for i=1, n - 1 do
		local j = math.random(i, n)
		t[i], t[j] = t[j], t[i]
	end
end

shuffle(threads)

local now1 = uv.now()

for i=1, #threads do
	threads[i]:run()
end

uv.update_time()
local now2 = uv.now()

print("took", now2 - now1)