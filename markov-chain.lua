local function words()
	local line = io.read()
	local i = 1
	return function()
		while line do
			local p0, p1 = string.find(line, "[^ ]+", i)
			if p0 then
				i = p1 + 1
				return string.sub(line, p0, p1)
			else
				line = io.read()
				i = 1
				return "\n" -- return end of prev line
			end
		end
	end
end

local state = {}
local function insert(k, v)
	if not state[k] then
		state[k] = {}
	end
	table.insert(state[k], v)
end

local function prefix(w1, w2)
	return w1 .. " " .. w2
end

local w1, w2 = "\n", "\n" -- start at end of paragraph
for w in words() do
	insert(prefix(w1, w2), w)
	w1, w2 = w2, w
end
insert(prefix(w1, w2), "\0")

--[[
for k, v in pairs(state) do
	print(k .. ":", table.concat(v, " "))
end
for i, v in ipairs(state["\n \n"]) do
	print(i, v)
end
--]]

w1, w2 = "\n", "\n"
local gen = {}
math.randomseed(os.time())
for i = 1, 1000 do
	local suf = state[prefix(w1, w2)]
	local w3 = suf[math.random(#suf)]
	if w3 == "\0" then break end
	if w2 ~= "\n" then table.insert(gen, " ") end
	table.insert(gen, w3)
	w1, w2 = w2, w3
end
print(table.concat(gen, ""))
