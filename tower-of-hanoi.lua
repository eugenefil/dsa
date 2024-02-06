local SPACER = 2
local PROG = arg[0]

local function _solve(n, cur, targ)
	local towers = {1, 2, 3}
	towers[cur] = nil
	towers[targ] = nil
	local oth = next(towers)

	if n == 2 then
		return {{1, cur, oth}, {2, cur, targ}, {1, oth, targ}}
	end
	
	-- move top n - 1 disks to other tower to get to bottom disk
	local res = _solve(n - 1, cur, oth)
	-- move bottom disk to target tower
	table.insert(res, {n, cur, targ})
	-- move n - 1 disks to target tower on top of bottom disk
	local fin = _solve(n - 1, oth, targ)
	for _, v in ipairs(fin) do
		table.insert(res, v)
	end
	return res
end

function solve(n)
	assert(n > 1)
	return _solve(n, 1, 3)
end

local function draw_frame(num, n, towers, imsize, termsize)
	local h, w = unpack(imsize)
	local ht, wt = unpack(termsize)
	local xc, yc = math.floor(wt / 2), math.floor(ht / 2)
	local x0, y0 = xc - math.floor(w / 2), yc - math.floor(h / 2) + SPACER
	
	io.stdout:write("\027[2J") -- clear screen

	-- draw tower bases
	for i in ipairs(towers) do
		local xt = x0 + (i - 1) * (n + SPACER)
		io.stdout:write(string.format("\027[%d;%dH%s",
			ht - (y0 - 1), xt, string.rep("-", n)))
	end

	for i, tower in ipairs(towers) do
		local xt = x0 + (i - 1) * (n + SPACER)
		for j, disk in ipairs(tower) do
			local yd = y0 + (j - 1)
			io.stdout:write(string.format(
				"\027[%d;%dH\027[%dm%s\027[m",
				ht - yd, xt, 30 + disk, string.rep("#", disk)))
		end
	end

	io.stdout:write(string.format(
		"\027[%d;%dHStep %d ",
		ht - (y0 - SPACER), x0, num))
	io.stdout:flush()
end

function solve_interactive(n, termsize, wait)
	local imsize = {n + SPACER, n * 3 + SPACER * (3 - 1)}
	local hmin, wmin = unpack(imsize)
	if termsize then
		local h, w = unpack(termsize)
		if h < hmin or w < wmin then
			io.stderr:write(string.format("Terminal size is (%d, %d), must be at least (%d, %d)\n", h, w, hmin, wmin))
			return
		end
	else
		termsize = {hmin, wmin} -- assume min size
	end

	local moves = solve(n)
	local towers = {{}, {}, {}}
	for i = n, 1, -1 do
		table.insert(towers[1], i)
	end
	draw_frame(0, n, towers, imsize, termsize)
	wait()

	for i, m in ipairs(moves) do
		local _, from, to = unpack(m)
		table.insert(towers[to], table.remove(towers[from]))
		draw_frame(i, n, towers, imsize, termsize)
		wait()
	end
end

local function get_termsize()
	local f = io.popen("stty size")
	local line = f:read()
	f:close()
	if line then
		local h, w = string.match(line, "(%d+) (%d+)")
		if h and w then
			return {h + 0, w + 0}
		end
	end
end

local function wait_key()
	io.stdout:write("Press any key")
	io.stdout:flush()
	io.read()
end

local function sleep(timeout)
	os.execute("sleep " .. timeout)
end

local function usage()
	print(string.format([[
usage: %s [OPTION]... [NUM_DISKS]
Print the solution to Tower of Hanoi with NUM_DISKS disks (default 4).

  -p[=PAUSE]    play solution frames continuously; pause for PAUSE
                  seconds between frames (default 1)
	]], PROG))
end

local wait = wait_key
local n = 4
for _, a in ipairs(arg) do
	if string.sub(a, 1, 2) == "-p" then
		local timeout = 1
		if #a > 2 then
			timeout = string.match(a, "^-p=(%d+)$") or
				string.match(a, "^-p=(%d+.%d+)$")
			if not timeout then
				usage()
				os.exit(1)
			end
			timeout = timeout + 0
		end
		wait = function() sleep(timeout) end
	elseif string.match(a, "^%d+$") then
		n = a + 0
	elseif a == '-h' or a == '--help' then
		usage()
		os.exit()
	else
		usage()
		os.exit(1)
	end
end

io.stdout:write("\027[s") -- save cursor pos
io.stdout:write("\027[?25l") -- hide cursor
solve_interactive(n, get_termsize(), wait)
io.stdout:write("\027[?25h") -- show cursor
io.stdout:write("\027[u") -- restore cursor pos
