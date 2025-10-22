// Copyright (C) 2025, Coudert--Osmont Yoann
// SPDX-License-Identifier: AGPL-3.0-or-later
// See <https://www.gnu.org/licenses/>

#pragma once

#include <exception>
#include <string>

struct OSMError : public std::exception {
	const std::string m_msg;
	OSMError(const std::string &msg, const std::string &file, int line):
		m_msg(msg + "\n    at " + file + ":" + std::to_string(line)) {}
	const char* what() const throw() { return m_msg.c_str(); }
};
#define THROW_ERROR(msg) throw OSMError((msg), __FILE__, __LINE__)
