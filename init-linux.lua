-- Performance optimizations (MUST be first)
vim.loader.enable() -- Faster Lua module loading (Neovim 0.9+)

-- Basic settings
vim.o.mouse = 'a' -- Enable mouse interaction
vim.o.number = true -- Line numbers
vim.o.relativenumber = true -- Relative line numbers
vim.o.tabstop = 2 -- 2 spaces for tabs
vim.o.shiftwidth = 2 -- 2 spaces for indentation
vim.o.expandtab = true -- Use spaces instead of tabs
vim.o.smartindent = true -- Smart indentation
vim.o.termguicolors = true -- Enable 24-bit RGB colors
vim.o.clipboard = 'unnamedplus' -- Use system clipboard
vim.o.updatetime = 250 -- Faster updates for responsiveness
vim.o.timeoutlen = 500 -- Time for key sequences
vim.o.cursorline = true -- Highlight current line
vim.o.signcolumn = 'yes' -- Always show sign column
vim.o.scrolloff = 8 -- Keep 8 lines visible above/below cursor

-- Additional performance settings
vim.o.lazyredraw = false -- Don't redraw during macros (set to true if you use macros often)
vim.o.swapfile = false -- Disable swap files for better performance
vim.o.backup = false -- Disable backup files
vim.o.writebackup = false -- Disable backup before overwriting
vim.o.undofile = true -- Enable persistent undo
vim.o.undodir = vim.fn.stdpath('data') .. '/undo' -- Undo directory

-- Key mappings
vim.g.mapleader = ' ' -- Set leader key to space

-- Tab management
vim.keymap.set('n', '<leader>tn', ':tabnew<CR>', { desc = 'New tab' })
vim.keymap.set('n', '<leader>tc', ':tabclose<CR>', { desc = 'Close tab' })
vim.keymap.set('n', '<leader>tl', ':tabnext<CR>', { desc = 'Next tab' })
vim.keymap.set('n', '<leader>th', ':tabprevious<CR>', { desc = 'Previous tab' })

vim.keymap.set('n', '<leader>te', ':term<CR>', { desc = 'Open Terminal'})
vim.keymap.set('t', '<leader>tq', '<C-\\><C-n><C-w>h', { desc = 'Close Terminal'})

-- Sexplore shortcut
vim.keymap.set('n', '<leader>e', ':Sexplore<CR>', { desc = 'Open Sexplore' })

-- Quality-of-life keymaps
vim.keymap.set('n', '<leader>w', ':wa<CR>', { desc = 'Save file' })
vim.keymap.set('n', '<leader>q', ':q<CR>', { desc = 'Quit' })
vim.keymap.set('n', '<leader>wq', ':wqa<CR>', { desc = 'Write and quit' })
vim.keymap.set('n', '<leader>qa', ':qa<CR>', { desc = 'Quit All' })
vim.keymap.set('n', '<leader>qq', ':qa!<CR>', { desc = 'Force Quit' })
vim.keymap.set('n', '<C-h>', '<C-w>h', { desc = 'Move to left window' })
vim.keymap.set('n', '<C-j>', '<C-w>j', { desc = 'Move to bottom window' })
vim.keymap.set('n', '<C-k>', '<C-w>k', { desc = 'Move to top window' })
vim.keymap.set('n', '<C-l>', '<C-w>l', { desc = 'Move to right window' })

vim.diagnostic.config({
  virtual_text = {
    prefix = "●",  -- could be "■", "▎", "x", etc.
    spacing = 2,   -- extra spaces before message
  },
  underline = true,      -- also underline problematic text
  update_in_insert = false,
})

-- Plugin manager: lazy.nvim
local lazypath = vim.fn.stdpath('data') .. '/lazy/lazy.nvim'
if not vim.loop.fs_stat(lazypath) then
  vim.fn.system({
    'git',
    'clone',
    '--filter=blob:none',
    'https://github.com/folke/lazy.nvim.git',
    '--branch=stable',
    lazypath,
  })
end
vim.opt.rtp:prepend(lazypath)

-- Plugins
require('lazy').setup({
  -- File explorer (modern alternative to NERDTree)
  {
    'nvim-tree/nvim-tree.lua',
    cmd = { 'NvimTreeToggle', 'NvimTreeFocus', 'NvimTreeFindFile', 'NvimTreeResize' },
    dependencies = { 'nvim-tree/nvim-web-devicons' },
    keys = {
      { '<leader>n', '<cmd>NvimTreeToggle<CR>', desc = 'Toggle file explorer' },
      { '<leader>n+', '<cmd>NvimTreeResize +10<CR>', desc = 'NvimTree +10' },
      { '<leader>n-', '<cmd>NvimTreeResize -10<CR>', desc = 'NvimTree -10' },
      { '<leader><CR>', '<C-]>', desc = 'Step into directory' },
    },
    config = function()
      require('nvim-tree').setup({
        view = { width = 40, side = 'left' },
        filters = { dotfiles = false },
      })
    end,
  },
  -- Git blame
  {
    "f-person/git-blame.nvim",
    event = "BufReadPost",
    keys = {
      { '<leader>gb', '<cmd>GitBlameToggle<CR>', desc = 'Git blame toggle' },
    },
    opts = {
        enabled = false,  -- Start disabled, toggle with <leader>gb
        message_template = " <summary> • <date> • <author> • <<sha>>",
        date_format = "%m-%d-%Y %H:%M:%S",
        virtual_text_column = 1,
    },
  },
  -- Navic
  {
    "SmiteshP/nvim-navic",
    lazy = true,
    dependencies = "neovim/nvim-lspconfig"
  },
  -- Github theme
  {
    'projekt0n/github-nvim-theme',
    lazy = false,
    priority = 1000,
    config = function()
      require('github-theme').setup({
        options = {
          transparent = false,
          styles = {
            comments = 'italic',
            functions = 'bold',
            keywords = 'bold',
            variables = 'NONE',
          },
        },
      })
      -- vim.cmd('colorscheme github_dark')
      end,
  },
  -- Kanso Theme
  {
    "webhooked/kanso.nvim",
    lazy = false,
    priority = 1000,
    config = function ()
      require('kanso').setup({
        transparent = true,
        termincalColors = true,
      })
      vim.cmd('colorscheme kanso')
      end,

  },
  -- Enhanced LSP UI
  {
    'nvimdev/lspsaga.nvim',
    event = 'LspAttach',
    config = function()
      require('lspsaga').setup({
        ui = {
          border = 'rounded',
          code_action = '',
        },
        lightbulb = {
          enable = true,
          sign = true,
          virtual_text = false,
        },
        symbol_in_winbar = {
          enable = false,
        },
      })
    end,
    dependencies = {
      'nvim-treesitter/nvim-treesitter',
      'nvim-tree/nvim-web-devicons',
    }
  },

  -- LSP support
  {
    'neovim/nvim-lspconfig',
    event = { 'BufReadPre', 'BufNewFile' },
    dependencies = {
      'williamboman/mason.nvim',
      'williamboman/mason-lspconfig.nvim',
      'hrsh7th/nvim-cmp',
      'hrsh7th/cmp-nvim-lsp',
      'L3MON4D3/LuaSnip',
    },
    config = function()
      -- Mason for LSP server management
      require('mason').setup()
      require('mason-lspconfig').setup({
        ensure_installed = {
          'jdtls', -- Java
          'pyright', -- Python
          'clangd', -- C/C++
          'gopls', -- Go
          'zls', -- Zig
          'asm_lsp', -- Assembly
        },
      })

      -- LSP server configurations using new vim.lsp.config API
      local navic = require('nvim-navic')
      local capabilities = require('cmp_nvim_lsp').default_capabilities()

      -- Common on_attach function for navic
      local on_attach = function(client, bufnr)
        if client.server_capabilities.documentSymbolProvider then
          navic.attach(client, bufnr)
        end
      end

      -- Java (Enhanced configuration)
      vim.lsp.config.jdtls = {
        default_config = {
          cmd = { 'jdtls' },
          filetypes = { 'java' },
          root_markers = { '.git', 'mvnw', 'gradlew', 'pom.xml', 'build.gradle' },
          capabilities = capabilities,
          on_attach = on_attach,
          settings = {
            java = {
              -- Code generation settings
              codeGeneration = {
                toString = {
                  template = "${object.className}{${member.name()}=${member.value}, ${otherMembers}}"
                },
                useBlocks = true,
              },
              -- Completion settings
              completion = {
                favoriteStaticMembers = {
                  "org.junit.Assert.*",
                  "org.junit.Assume.*",
                  "org.junit.jupiter.api.Assertions.*",
                  "org.junit.jupiter.api.Assumptions.*",
                  "org.junit.jupiter.api.DynamicContainer.*",
                  "org.junit.jupiter.api.DynamicTest.*",
                  "org.mockito.Mockito.*",
                  "org.mockito.ArgumentMatchers.*",
                  "org.mockito.Answers.*"
                },
                filteredTypes = {
                  "com.sun.*",
                  "io.micrometer.shaded.*",
                  "java.awt.*",
                  "jdk.*",
                  "sun.*",
                },
                importOrder = {
                  "java",
                  "javax",
                  "org",
                  "com",
                },
              },
              -- Import settings
              sources = {
                organizeImports = {
                  starThreshold = 9999,
                  staticStarThreshold = 9999,
                },
              },
              -- Formatting
              format = {
                enabled = true,
                settings = {
                  url = vim.fn.stdpath("config") .. "/lang-servers/intellij-java-google-style.xml",
                  profile = "GoogleStyle",
                },
              },
              -- Configuration and runtime
              configuration = {
                runtimes = {
                  {
                    name = "JavaSE-11",
                    path = "/usr/lib/jvm/java-11-openjdk/",
                  },
                  {
                    name = "JavaSE-17",
                    path = "/usr/lib/jvm/java-17-openjdk/",
                  },
                  {
                    name = "JavaSE-21",
                    path = "/usr/lib/jvm/java-21-openjdk/",
                  },
                }
              },
              -- Maven settings
              maven = {
                downloadSources = true,
              },
              -- Eclipse settings
              eclipse = {
                downloadSources = true,
              },
              -- References code lens
              referencesCodeLens = {
                enabled = true,
              },
              -- Inlay hints
              inlayHints = {
                parameterNames = {
                  enabled = "all",
                },
              },
              -- Signature help
              signatureHelp = {
                enabled = true,
              },
            }
          },
        },
      }
      vim.lsp.enable('jdtls')

      -- Python
      vim.lsp.config.pyright = {
        default_config = {
          cmd = { 'pyright-langserver', '--stdio' },
          filetypes = { 'python' },
          root_markers = { 'pyproject.toml', 'setup.py', 'setup.cfg', 'requirements.txt', 'Pipfile', '.git' },
        },
      }
      vim.lsp.enable('pyright')

      -- C/C++
      vim.lsp.config.clangd = {
        default_config = {
          cmd = { 'clangd' },
          filetypes = { 'c', 'cpp', 'objc', 'objcpp', 'cuda', 'proto' },
          root_markers = { '.clangd', '.clang-tidy', '.clang-format', 'compile_commands.json', 'compile_flags.txt', 'configure.ac', '.git' },
        },
      }
      vim.lsp.enable('clangd')

      -- Go
      vim.lsp.config.gopls = {
        default_config = {
          cmd = { 'gopls' },
          filetypes = { 'go', 'gomod', 'gowork', 'gotmpl' },
          root_markers = { 'go.work', 'go.mod', '.git' },
        },
      }
      vim.lsp.enable('gopls')

      -- Zig
      vim.lsp.config.zls = {
        default_config = {
          cmd = { 'zls' },
          filetypes = { 'zig', 'zir' },
          root_markers = { 'zls.json', '.git', 'build.zig' },
          settings = {
            zls = {
              enable_autofix = true,
              enable_snippets = true,
              warn_style = true,
            }
          },
        },
      }
      vim.lsp.enable('zls')

      -- Assembly
      vim.lsp.config.asm_lsp = {
        default_config = {
          cmd = { 'asm-lsp' },
          filetypes = { 'asm', 's', 'S' },
          root_markers = { '.git' },
        },
      }
      vim.lsp.enable('asm_lsp')

      -- Keymaps for LSP
      vim.api.nvim_create_autocmd('LspAttach', {
        group = vim.api.nvim_create_augroup('UserLspConfig', {}),
        callback = function(ev)
          local opts = { buffer = ev.buf, silent = true }

          -- Navigation
          vim.keymap.set('n', 'gd', '<cmd>Lspsaga goto_definition<CR>', opts)
          vim.keymap.set('n', 'gD', vim.lsp.buf.declaration, opts)
          vim.keymap.set('n', 'gi', vim.lsp.buf.implementation, opts)
          vim.keymap.set('n', 'gt', '<cmd>Lspsaga goto_type_definition<CR>', opts)
          vim.keymap.set('n', 'gp', '<cmd>Lspsaga peek_definition<CR>', opts)
          vim.keymap.set('n', '<leader>fr', '<cmd>Lspsaga finder<CR>', opts)

          -- Documentation
          vim.keymap.set('n', 'K', '<cmd>Lspsaga hover_doc<CR>', opts)
          vim.keymap.set('n', '<C-k>', vim.lsp.buf.signature_help, opts)
          vim.keymap.set('i', '<C-k>', vim.lsp.buf.signature_help, opts)

          -- Code actions and refactoring
          vim.keymap.set('n', '<leader>ca', '<cmd>Lspsaga code_action<CR>', opts)
          vim.keymap.set('n', '<leader>rn', '<cmd>Lspsaga rename<CR>', opts)

          -- Diagnostics
          vim.keymap.set('n', '[d', '<cmd>Lspsaga diagnostic_jump_prev<CR>', opts)
          vim.keymap.set('n', ']d', '<cmd>Lspsaga diagnostic_jump_next<CR>', opts)
          vim.keymap.set('n', '<leader>d', '<cmd>Lspsaga show_line_diagnostics<CR>', opts)

          -- Telescope integration for symbols
          vim.keymap.set('n', '<leader>ds', ':Telescope lsp_document_symbols<CR>', opts)
          vim.keymap.set('n', '<leader>ws', ':Telescope lsp_workspace_symbols<CR>', opts)
        end,
      })
    end,
  },

  -- Autocompletion
  {
    'hrsh7th/nvim-cmp',
    event = 'InsertEnter',
    dependencies = {
      'hrsh7th/cmp-nvim-lsp',
      'hrsh7th/cmp-buffer',
      'hrsh7th/cmp-path',
      'saadparwaiz1/cmp_luasnip',
      'L3MON4D3/LuaSnip',
    },
    config = function()
      local cmp = require('cmp')
      local luasnip = require('luasnip')

      cmp.setup({
        snippet = {
          expand = function(args)
            luasnip.lsp_expand(args.body)
          end,
        },
        window = {
          completion = cmp.config.window.bordered(),
          documentation = cmp.config.window.bordered(),
        },
        formatting = {
          format = function(entry, vim_item)
            -- Kind icons
            local kind_icons = {
              Text = "",
              Method = "󰆧",
              Function = "󰊕",
              Constructor = "",
              Field = "󰇽",
              Variable = "󰂡",
              Class = "󰠱",
              Interface = "",
              Module = "",
              Property = "󰜢",
              Unit = "",
              Value = "󰎠",
              Enum = "",
              Keyword = "󰌋",
              Snippet = "",
              Color = "󰏘",
              File = "󰈙",
              Reference = "",
              Folder = "󰉋",
              EnumMember = "",
              Constant = "󰏿",
              Struct = "",
              Event = "",
              Operator = "󰆕",
              TypeParameter = "󰅲",
            }
            vim_item.kind = string.format('%s %s', kind_icons[vim_item.kind] or "", vim_item.kind)
            vim_item.menu = ({
              nvim_lsp = "[LSP]",
              luasnip = "[Snippet]",
              buffer = "[Buffer]",
              path = "[Path]",
            })[entry.source.name]
            return vim_item
          end
        },
        mapping = cmp.mapping.preset.insert({
          ['<C-b>'] = cmp.mapping.scroll_docs(-4),
          ['<C-f>'] = cmp.mapping.scroll_docs(4),
          ['<C-Space>'] = cmp.mapping.complete(),
          ['<CR>'] = cmp.mapping.confirm({ select = true }),
          ['<Tab>'] = cmp.mapping(function(fallback)
            if cmp.visible() then
              cmp.select_next_item()
            elseif luasnip.expand_or_jumpable() then
              luasnip.expand_or_jump()
            else
              fallback()
            end
          end, { 'i', 's' }),
          ['<S-Tab>'] = cmp.mapping(function(fallback)
            if cmp.visible() then
              cmp.select_prev_item()
            elseif luasnip.jumpable(-1) then
              luasnip.jump(-1)
            else
              fallback()
            end
          end, { 'i', 's' }),
        }),
        sources = cmp.config.sources({
          { name = 'nvim_lsp', priority = 1000 },
          { name = 'luasnip', priority = 750 },
          { name = 'buffer', priority = 500 },
          { name = 'path', priority = 250 },
        }),
      })
    end,
  },

  -- Syntax highlighting with Treesitter
  {
    'nvim-treesitter/nvim-treesitter',
    event = { 'BufReadPost', 'BufNewFile' },
    build = ':TSUpdate',
    config = function()
      require('nvim-treesitter.configs').setup({
        ensure_installed = { 'java', 'python', 'c', 'cpp', 'go', 'lua', 'vim', 'zig' },
        highlight = {
          enable = true,
          additional_vim_regex_highlighting = false,
        },
        indent = { enable = true },
      })
    end,
  },

  -- Fuzzy finder
  {
    'nvim-telescope/telescope.nvim',
    cmd = 'Telescope',
    keys = {
      { '<leader>ff', '<cmd>Telescope find_files<CR>', desc = 'Find files' },
      { '<leader>fg', '<cmd>Telescope live_grep<CR>', desc = 'Search in files' },
      { '<leader>fd', '<cmd>Telescope diagnostics<CR>', desc = 'Telescope diagnostics' },
    },
    dependencies = { 'nvim-lua/plenary.nvim' },
    config = function()
      require('telescope').setup({
        defaults = {
          mappings = {
            i = {
              ['<C-j>'] = 'move_selection_next',
              ['<C-k>'] = 'move_selection_previous',
            },
          },
        },
      })
    end,
  },

  -- Statusline
  {
    'nvim-lualine/lualine.nvim',
    dependencies = { 'nvim-tree/nvim-web-devicons' },
    config = function()
      require('lualine').setup({
        options = {
          theme = 'auto',
          section_separators = { left = '', right = '' },
          component_separators = { left = '', right = '' },
        },
        sections = {
          lualine_a = { 'mode' },
          lualine_b = { 'branch', 'diff', 'diagnostics' },
          lualine_c = { 'filename' },
          lualine_x = { 'encoding', 'fileformat', 'filetype' },
          lualine_y = { 'progress' },
          lualine_z = { 'location' },
        },
      })
    end,
  },

  -- Quality-of-life plugins
  {
    'tpope/vim-commentary',
    keys = {
      { 'gcc', mode = 'n', desc = 'Comment line' },
      { 'gc', mode = 'v', desc = 'Comment selection' },
    },
  },
  {
    'windwp/nvim-autopairs',
    event = 'InsertEnter',
    config = function()
      require('nvim-autopairs').setup()
    end,
  },
  {
    'norcalli/nvim-colorizer.lua',
    event = 'BufReadPost',
    config = function()
      require('colorizer').setup()
    end,
  },
}, {
  -- Lazy.nvim performance settings
  performance = {
    cache = {
      enabled = true,
    },
    rtp = {
      disabled_plugins = {
        "gzip",
        "matchit",
        "matchparen",
        "netrwPlugin",
        "tarPlugin",
        "tohtml",
        "tutor",
        "zipPlugin",
      },
    },
  },
  checker = {
    enabled = false, -- Don't auto-check for updates
  },
})

-- Additional quality-of-life settings
vim.api.nvim_create_autocmd('TextYankPost', {
  callback = function()
    vim.highlight.on_yank({ higroup = 'IncSearch', timeout = 200 })
  end,
}) -- Highlight yanked text

vim.o.winbar = "%{%v:lua.require'nvim-navic'.get_location()%}"
