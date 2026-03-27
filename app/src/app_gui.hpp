// Tzopilotl
// Copyright (C) 2026 James McCartney
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

//
//  app_gui.hpp
//  app
//
//  GUI mode entry point for the live coding application.
//

#ifndef app_gui_hpp
#define app_gui_hpp

namespace bridge { struct AppContext; }

// Run the GUI application. Returns exit code.
// Blocks until the window is closed or a signal is received.
int runGui(bridge::AppContext& appCtx);

#endif /* app_gui_hpp */
