#pragma once

namespace draxul::nvim_lua
{

inline constexpr const char* kCopySelection = R"(
local ch = ...
local lines = vim.fn.getreg('"', true, true)
local regtype = vim.fn.getregtype('"')
vim.fn.rpcnotify(ch, 'clipboard_set', '"', lines, regtype)
)";

inline constexpr const char* kOpenFileAtFunction = R"(
local path, qualified, func = ...
vim.cmd.edit(vim.fn.fnameescape(path))
vim.cmd('normal! gg')
if qualified ~= '' then
    local qualified_pat = qualified .. '::' .. func .. [[\s*(]]
    if vim.fn.search(qualified_pat) > 0 then return end
end
local bare_pat = [[\<]] .. func .. [[\s*(]]
if vim.fn.search(bare_pat) > 0 then return end
local struct_pat = [[\<\(struct\|class\|enum\|union\)\s\+]] .. func .. [[\>]]
vim.fn.search(struct_pat)
)";

inline constexpr const char* kOpenFileAtType = R"(
local path, qualified = ...
vim.cmd.edit(vim.fn.fnameescape(path))
vim.cmd('normal! gg')
local name = qualified
local last = qualified:match(".*::([^:]+)$")
if last ~= nil and last ~= '' then
    name = last
end
local type_pat = [[\<\(struct\|class\|enum\|union\)\s\+]] .. name .. [[\>]]
if vim.fn.search(type_pat) > 0 then return end
local using_pat = [[\<using\s\+]] .. name .. [[\>]]
if vim.fn.search(using_pat) > 0 then return end
local typedef_pat = [[\<typedef\>.\{-}\<]] .. name .. [[\>]]
vim.fn.search(typedef_pat)
)";

inline constexpr const char* kOpenFile = "vim.cmd.edit(vim.fn.fnameescape(...))";

inline constexpr const char* kClipboardProvider = R"(
local channel = ...
vim.g.clipboard = {
  name = 'draxul',
  copy = {
    ['+'] = function(lines, regtype) vim.fn.rpcnotify(channel, 'clipboard_set', '+', lines, regtype) end,
    ['*'] = function(lines, regtype) vim.fn.rpcnotify(channel, 'clipboard_set', '*', lines, regtype) end,
  },
  paste = {
    ['+'] = function() return vim.fn.rpcrequest(channel, 'clipboard_get', '+') end,
    ['*'] = function() return vim.fn.rpcrequest(channel, 'clipboard_get', '*') end,
  },
  cache_enabled = 0,
}
)";

} // namespace draxul::nvim_lua
