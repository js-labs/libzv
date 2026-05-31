/*
 * Copyright (C) 2022 Sergey Zubarev, info@js-labs.org
 *
 * This file is a part of ZV framework.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#if !defined(ZV_EXPORT_H)
#define ZV_EXPORT_H

#if defined(_WIN32)
    #if defined(ZV_SUPPRESS_EXPORT)
        #define ZV_EXPORT
    #else
        #if defined(BUILDING_ZV_DLL)
            #define ZV_EXPORT __declspec(dllexport)
        #else
            #define ZV_EXPORT __declspec(dllimport)
        #endif
    #endif
#else
    #define ZV_EXPORT
#endif

#endif /* !defined(ZV_EXPORT_H) */
