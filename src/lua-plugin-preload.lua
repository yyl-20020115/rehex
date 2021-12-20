--- Reverse Engineers' Hex Editor Lua plugin API
-- @module rehex

local registrations = {};

--- Register a function to be called during app initialisation.
--
-- @param callback Callback function.
--
-- The given function will be called during the REHex::App::SetupPhase::READY hook (i.e. after most
-- initialisation is complete, but before the MainWindow is created.

rehex.OnAppReady = function(callback)
	local registration = rehex.REHex_App_SetupHookRegistration.new(rehex.App_SetupPhase_READY, callback);
	table.insert(registrations, registration);
end

--- Register a function to be called during app initialisation.
--
-- @param callback Callback function.
--
-- The given function will be called during the REHex::App::SetupPhase::DONE hook (i.e. after the
-- MainWindow has been created and any files opened.

rehex.OnAppDone = function(callback)
	local registration = rehex.REHex_App_SetupHookRegistration.new(rehex.App_SetupPhase_DONE, callback);
	table.insert(registrations, registration);
end

--- Register a function to be called when a new tab is created.
--
-- @param callback Callback function.
--
-- Registers a function to be called whenever a file is opened or a new one created.
-- This should typically be called during your plugin's initialisation.
--
-- The callback function will be passed references to the MainWindow and Tab objects.

rehex.OnTabCreated = function(callback)
	local registration = rehex.REHex_MainWindow_SetupHookRegistration.new(
		rehex.MainWindow_SetupPhase_DONE,
		function(mainwindow)
			mainwindow:Connect(rehex["REHex::TAB_CREATED"], function(event)
				callback(event:GetEventObject(), event.tab)
				event:Skip() -- Continue propagation
			end);
		end);
	
	table.insert(registrations, registration);
end

--- Add a command to the "Tools" menu.
--
-- @param label Label to display in the menu.
-- @param callback Callback function to be called when the command is activated.
--
-- Adds a command to the "Tools" menu of the main window. This must be called before the window is
-- constructed, usually as part of your plugin's initialisation.
--
-- The callback function will be passed a reference to the MainWindow object when activated.

rehex.AddToToolsMenu = function(label, callback)
	local registration = rehex.REHex_MainWindow_SetupHookRegistration.new(
		rehex.MainWindow_SetupPhase_TOOLS_MENU_BOTTOM,
		function(mainwindow)
			local tools = mainwindow:get_tools_menu();
			
			local item = tools:Append(wx.wxID_ANY, label);
			local id = item:GetId();
			
			mainwindow:Connect(id, wx.wxEVT_MENU, function(event)
				callback(mainwindow)
			end);
		end);
	
	table.insert(registrations, registration);
end

if _rehex_plugin_dir ~= nil then
	--- Path to plugin data directory, nil if the plugin is a script in the top-level plugins
	--  directory.
	rehex.PLUGIN_DIR = _rehex_plugin_dir
	_rehex_plugin_dir = nil
	
	package.path = rehex.PLUGIN_DIR .. "/?.lua;" .. package.path
end

-- Bodge in some less-obnoxious aliases for the classes generated by genwxbind.lua
rehex.Comment = rehex.REHex_Document_Comment
rehex.Document = rehex.REHex_Document
rehex.MainWindow = rehex.REHex_MainWindow
rehex.Tab = rehex.REHex_Tab