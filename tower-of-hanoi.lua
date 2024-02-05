local function _solve(n, cur, targ)
	local towers = {1, 2, 3}
	towers[cur] = nil
	towers[targ] = nil
	local oth = next(towers)

	if n == 2 then
		return {{1, cur, oth}, {2, cur, targ}, {1, oth, targ}}
	end
	
	local res = _solve(n - 1, cur, oth) -- move n - 1 disks to other tower
	table.insert(res, {n, cur, targ}) -- move last disk to target tower
	local fin = _solve(n - 1, oth, targ) -- move n - 1 disks to target tower
	for _, v in ipairs(fin) do
		table.insert(res, v)
	end
	return res
end

function solve(n)
	assert(n > 1)
	return _solve(n, 1, 3)
end
