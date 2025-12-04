// Copyright (C) 2025, Coudert--Osmont Yoann
// SPDX-License-Identifier: AGPL-3.0-or-later
// See <https://www.gnu.org/licenses/>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <ranges>
#include <unordered_map>
#include <vector>

using namespace std;

const vector<const char*>
	int_t,
	place {"city"},
	highway {"motorway", "trunk", "primary"},
	waterway {"river"},
	boundary {"administrative"},
	landuse {"forest"},
	natural {"wood"},
	network {"FR:A-road", "FR:N-road"};

using Key = pair<const char*, const vector<const char*>*>;

Key nodeKeys[] {
	{"place", &place},
	{"name", nullptr},
	{"capital", &int_t},
};

Key wayKeys[] {
	{"highway", &highway},
	{"waterway", &waterway},
	{"boundary", &boundary},
	{"admin_level", &int_t},
	{"landuse", &landuse},
	{"natural", &natural},
};

pair<const char*, vector<Key>> relationKeys[] {
	{"waterway", {
		{"waterway", &waterway},
		{"ref:sandre", nullptr},
	}}, {"route", {
		{"network", &network},
		{"ref", nullptr},
	}}, {"multipolygon", {
		{"landuse", &landuse},
	}},
};

int main(int argc, char* argv[]) {
	if(argc != 2) {
		cerr << "Usage:\n";
		cerr << ">> " << argv[0] << " output-folder`\n";
		cerr << "This will produce two files `enums.h` and `enums.cpp` in the folder `output-folder`\n";
		return 1;
	}
	const filesystem::path outputDir = argv[1];

	unordered_map<const vector<const char*>*, const char*> enumNames;
	const auto searchKey = [&](const Key &k) {
		const auto it = enumNames.find(k.second);
		if(it == enumNames.end())
			enumNames[k.second] = k.first;
		else if(strlen(k.first) < strlen(it->second))
			it->second = k.first;
	};
	for(const Key &k : nodeKeys) searchKey(k);
	for(const Key &k : wayKeys) searchKey(k);
	for(const vector<Key> &ks : relationKeys | views::values)
		for(const Key &k : ks) searchKey(k);
	

	ofstream Hfile(outputDir / "enums.h");
	Hfile << R"lim(#pragma once

#include <cstdint>
#include <string_view>

#include "converter/string_switch.h"
)lim";
	const auto toClass = [&](const char* v) {
		string s = v;
		s[0] = toupper(s[0]);
		return s;
	};
	const auto toENUM = [&](const char* v) {
		string s = v;
		for(char &c : s) {
			if(isalnum(c)) c = toupper(c);
			else c = '_';
		}
		return s;
	};
	const auto toMember = [&](const char* v) {
		string s = v;
		for(char &c : s) {
			if(!isalnum(c)) c = '_';
		}
		return s;
	};
	for(const auto &[values, name] : enumNames) {
		if(!values || values == &int_t) continue;
		Hfile << "\nenum class " << toClass(name) << " : uint32_t {\n";
		for(const char *v : *values)
			Hfile << "\t" << toENUM(v) << ",\n";
		Hfile << "};\n";
	}
	Hfile << "\nenum class RelationType : uint32_t {\n";
	for(const char *v : relationKeys | views::keys)
		Hfile << "\t" << toENUM(v) << ",\n";
	Hfile << "};\n";
	const auto writeTags = [&](const auto &keys, const char* tabs) {
		for(const auto &[name, t] : keys) {
			Hfile << tabs;
			if(!t) Hfile << "std::string_view";
			else if(t == &int_t) Hfile << "int";
			else Hfile << toClass(enumNames[t]);
			Hfile << " " << toMember(name);
			if(t == &int_t) Hfile << " = -1";
			else if(t) Hfile << " = (" << toClass(enumNames[t]) << ") UNDEF";
			Hfile << ";\n";
		}
		Hfile << tabs << "void readTag(const std::string_view &key, const std::string_view &val);\n";
	};
	Hfile << "\nstruct NodeTags {\n";
	Hfile << "\tstatic inline constexpr uint32_t UNDEF = StringSwitch::NOT_FOUND;\n";
	writeTags(nodeKeys, "\t");
	Hfile << "};\n";
	Hfile << "\nstruct WayTags {\n";
	Hfile << "\tstatic inline constexpr uint32_t UNDEF = StringSwitch::NOT_FOUND;\n";
	writeTags(wayKeys, "\t");
	Hfile << "};\n";
	Hfile << "\nstruct RelationTags {\n";
	Hfile << "\tstatic inline constexpr uint32_t UNDEF = StringSwitch::NOT_FOUND;\n";
	Hfile << "\tRelationType type = (RelationType) UNDEF;\n";
	Hfile << "\tunion {\n";
	for(const auto &[rt, ks] : relationKeys) {
		Hfile << "\t\tstruct { // " << rt << "\n";
		writeTags(ks, "\t\t\t");
		Hfile << "\t\t} " << rt << ";\n";
	}
	Hfile << "\t};\n";
	Hfile << "\tRelationTags() {};\n";
	Hfile << "\tvoid init();\n";
	Hfile << "\tvoid readType(const std::string_view &val);\n";
	Hfile << "\tvoid readTag(const std::string_view &key, const std::string_view &val);\n";
	Hfile << "};\n";
	Hfile.close();

	ofstream Cfile(outputDir / "enums.cpp");
	Cfile << R"lim(#include "enums.h"

#include <charconv>

#include "utils.h"

using namespace std;
)lim";

	for(const auto &[values, name] : enumNames) {
		if(!values || values == &int_t) continue;
		Cfile << "\nstatic const StringSwitch " << name << "Switch({\n";
		for(const char *v : *values)
			Cfile << "\t{\"" << v << "\", (uint32_t) " << toClass(name) << "::" << toENUM(v) << "},\n";
		Cfile << "});\n";
	}

	Cfile << "\nstatic const StringSwitch relationTypeSwitch({\n";
	for(const char *v : relationKeys | views::keys)
		Cfile << "\t{\"" << v << "\", (uint32_t) RelationType::" << toENUM(v) << "},\n";
	Cfile << "});\n";

	const auto writeRead = [&](const auto &keys) {
		for(const auto &[v, t] : keys) {
			Cfile << "\tcase " << toENUM(v) << ":\n";
			if(!t) {
				Cfile << "\t\t" << toMember(v) << " = val;\n";
			} else if(t == &int_t) {
				Cfile << "\t\tif(from_chars(val.begin(), val.end(), " << toMember(v) << ").ec != errc())\n";
				Cfile << "\t\t\tTHROW_ERROR(\"" << v << " is not a number: \" + string(val));\n";
			} else {
				const char* tn = enumNames[t];
				Cfile << "\t\t" << toMember(v) << " = (" << toClass(tn) << ") " << tn << "Switch.feed(val);\n";
			}
			Cfile << "\t\tbreak;\n";
		}
		Cfile << "\tdefault:\n";
		Cfile << "\t\tbreak;\n";
		Cfile << "\t}\n";
		Cfile << "}\n";
	};

	Cfile << "\nenum class NodeKey : uint32_t {\n";
	for(const char *v : nodeKeys | views::keys)
		Cfile << "\t" << toENUM(v) << ",\n";
	Cfile << "};\n";
	Cfile << "\nstatic const StringSwitch nodeKeySwitch({\n";
	for(const char *v : nodeKeys | views::keys)
		Cfile << "\t{\"" << v << "\", (uint32_t) NodeKey::" << toENUM(v) << "},\n";
	Cfile << "});\n";
	Cfile << "\nvoid NodeTags::readTag(const string_view &key, const string_view &val) {\n";
	Cfile << "\tswitch((NodeKey) nodeKeySwitch.feed(key)) {\n";
	Cfile << "\tusing enum NodeKey;\n";
	writeRead(nodeKeys);

	Cfile << "\nenum class WayKey : uint32_t {\n";
	for(const char *v : wayKeys | views::keys)
		Cfile << "\t" << toENUM(v) << ",\n";
	Cfile << "};\n";
	Cfile << "\nstatic const StringSwitch wayKeySwitch({\n";
	for(const char *v : wayKeys | views::keys)
		Cfile << "\t{\"" << v << "\", (uint32_t) WayKey::" << toENUM(v) << "},\n";
	Cfile << "});\n";
	Cfile << "\nvoid WayTags::readTag(const string_view &key, const string_view &val) {\n";
	Cfile << "\tswitch((WayKey) wayKeySwitch.feed(key)) {\n";
	Cfile << "\tusing enum WayKey;\n";
	writeRead(wayKeys);

	for(const auto &[rt, ks] : relationKeys) {
		Cfile << "\nenum class " << toClass(rt) << "Key : uint32_t {\n";
		for(const char *v : ks | views::keys)
			Cfile << "\t" << toENUM(v) << ",\n";
		Cfile << "};\n";
		Cfile << "\nstatic const StringSwitch " << rt << "KeySwitch({\n";
		for(const char *v : ks | views::keys)
			Cfile << "\t{\"" << v << "\", (uint32_t) " << toClass(rt) << "Key::" << toENUM(v) << "},\n";
		Cfile << "});\n";
		Cfile << "\nvoid decltype(RelationTags{}." << rt << ")::readTag(const string_view &key, const string_view &val) {\n";
		Cfile << "\tswitch((" << toClass(rt) << "Key) " << rt << "KeySwitch.feed(key)) {\n";
		Cfile << "\tusing enum " << toClass(rt) << "Key;\n";
		writeRead(ks);
	}

	Cfile << "\nvoid RelationTags::init() {\n";
	Cfile << "\tswitch(type) {\n";
	Cfile << "\tusing enum RelationType;\n";
	for(const char *v : relationKeys | views::keys) {
		Cfile << "\tcase " << toENUM(v) << ":\n";
		Cfile << "\t\tnew(&" << toMember(v) << ") decltype(" << toMember(v) << ")();\n";
		Cfile << "\t\tbreak;\n";
	}
	Cfile << "\tdefault:\n";
	Cfile << "\t\tbreak;\n";
	Cfile << "\t}\n";
	Cfile << "}\n";

	Cfile << "\nvoid RelationTags::readType(const string_view &val) {\n";
	Cfile << "\ttype = (RelationType) relationTypeSwitch.feed(val);\n";
	Cfile << "}\n";

	Cfile << "\nvoid RelationTags::readTag(const string_view &key, const string_view &val) {\n";
	Cfile << "\tswitch(type) {\n";
	Cfile << "\tusing enum RelationType;\n";
	for(const char *v : relationKeys | views::keys) {
		Cfile << "\tcase " << toENUM(v) << ":\n";
		Cfile << "\t\t" << toMember(v) << ".readTag(key, val);";
		Cfile << "\t\tbreak;\n";
	}
	Cfile << "\tdefault:\n";
	Cfile << "\t\tbreak;\n";
	Cfile << "\t}\n";
	Cfile << "}\n";

}