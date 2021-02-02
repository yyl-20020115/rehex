/* Reverse Engineer's Hex Editor
 * Copyright (C) 2021 Daniel Collins <solemnwarning@solemnwarning.net>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include "../src/platform.hpp"

#include <gtest/gtest.h>

#include "../src/App.hpp"
#include "../src/LuaPluginLoader.hpp"

using namespace REHex;

TEST(LuaPluginLoader, LoadPlugin)
{
	LuaPluginLoader::init();
	
	App &app = wxGetApp();
	app.console->clear();
	
	{
		LuaPlugin p = LuaPluginLoader::load_plugin("tests/stub-plugin.lua");
		
		EXPECT_EQ(app.console->get_messages_text(), "stub plugin loaded\n");
		app.console->clear();
	}
	
	EXPECT_EQ(app.console->get_messages_text(), "stub plugin unloaded\n");
	
	LuaPluginLoader::shutdown();
}
