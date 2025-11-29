// Copyright (C) 2025, Coudert--Osmont Yoann
// SPDX-License-Identifier: AGPL-3.0-or-later
// See <https://www.gnu.org/licenses/>

#include <iostream>
#include <fstream>
#include <cstdint>
#include <vector>
#include <unordered_set>
#include <filesystem>

using namespace std;

struct Field {
	enum Label {
		REQUIRED,
		OPTIONAL,
		REPEATED,
		ONEOF,
		PACKED
	};

	enum Type {
		MSG,
		ENUM,
		STRING,
		BYTES,
		BOOL,
		INT32,
		INT64,
		SINT32,
		SINT64,
		UINT32
	};

	struct Case {
		Type type;
		string name;
		uint32_t number;
	};

	Label label;
	Type type;
	string msgTypeName;
	string name;
	uint32_t number;
	vector<Case> cases;
	bool non_default = false;
	bool defaultOption = false;
	union {
		int32_t i32;
		int64_t i64;
	} defaultValue;
};

struct Enum {
	string name;
	vector<pair<string, int>> values;
};

struct Message {
	string name;
	vector<Enum> enums;
	vector<Field> fields;
	bool non_default = false;
};

unordered_set<string> msgNames;
unordered_set<Field::Type> canPacked {
	Field::Type::ENUM,
	Field::Type::BOOL,
	Field::Type::INT32,
	Field::Type::UINT32,
	Field::Type::SINT32,
	Field::Type::SINT64,
};

Field::Type getType(const string &word, const Message &msg) {
	if(word == "string") return Field::Type::STRING;
	if(word == "bytes") return Field::Type::BYTES;
	if(word == "bool") return Field::Type::BOOL;
	if(word == "int32") return Field::Type::INT32;
	if(word == "int64") return Field::Type::INT64;
	if(word == "sint32") return Field::Type::SINT32;
	if(word == "sint64") return Field::Type::SINT64;
	if(word == "uint32") return Field::Type::UINT32;
	if(msgNames.count(word)) return Field::Type::MSG;
	for(const Enum &e : msg.enums)
		if(e.name == word)
			return Field::Type::ENUM;
	cerr << "Unknown field type: " << word << " (" << __FILE__ << ':' << __LINE__ << ")\n";
	exit(1);
}

void readField(istream &in, Message &msg, Field::Label label) {
	Field &field = msg.fields.emplace_back();
	field.label = label;
	string word;
	while(in >> word && word.starts_with("//")) getline(in, word);
	field.type = getType(word, msg);
	if(field.type == Field::Type::MSG || field.type == Field::Type::ENUM)
		field.msgTypeName = word;
	while(in >> word && word.starts_with("//")) getline(in, word);
	field.name = word;
	while(in >> word && word.starts_with("//")) getline(in, word);
	if(word != "=") {
		cerr << "Expected `=`, got " << word << endl;
		exit(1);	
	};
	while(in >> word && word.starts_with("//")) getline(in, word);
	const auto readNumber = [&]() {
		int num = stoi(word);
		if(num < 1 || num >= (1<<29)) {
			cerr << "Bad field number: " << num << endl;
			exit(1);
		}
		field.number = num;
	};
	if(!word.empty() && word.back() == ';') {
		word.pop_back();
		readNumber();
	} else {
		readNumber();
		while(in >> word && word.starts_with("//")) getline(in, word);
		if(word == ";") return;
		if(word == "[packed") {
			in >> word;
			if(word != "=") {
				cerr << "Expected `=`, got " << word << endl;
				exit(1);	
			};
			in >> word;
			if(word != "true];") {
				cerr << "Expected `true];`, got " << word << endl;
				exit(1);	
			};
			if(field.label != Field::Label::REPEATED) {
				cerr << "Packed option is only available for repeated fields\n";
				exit(1);
			}
			if(!canPacked.count(field.type)) {
				cerr << "Packed option is not available for the type " << field.type << " (" << __FILE__ << ':' << __LINE__ << ')' << endl;
				exit(1);
			}
			field.label = Field::Label::PACKED;
			return;
		}
		if(word.starts_with("[default")) {
			if(word.size() >= sizeof("[default"))
				word = word.substr(sizeof("[default")-1);
			else
				in >> word;
			if(word.empty() || word[0] != '=') {
				cerr << "Expected `=`, got " << word << endl;
				exit(1);	
			};
			if(word.size() > 1)
				word = word.substr(1);
			else
				in >> word;
			if(word.ends_with("];")) {
				word.resize(word.size()-2);
			} else {
				string word2;
				in >> word2;
				if(word2 != "];") {
					cerr << "Expected `];`, got " << word2 << endl;
					exit(1);	
				}
			}
			if(field.label != Field::Label::OPTIONAL) {
				cerr << "Default option is only available for optional fields\n";
				exit(1);
			}
			switch(field.type) {
			case Field::Type::INT32:
				field.defaultValue.i32 = stoi(word);
				break;
			case Field::Type::INT64:
				field.defaultValue.i64 = stoll(word);
				break;
			default:
				cerr << "Default option is not available for the type " << field.type << " (" << __FILE__ << ':' << __LINE__ << ')' << endl;
				exit(1);
			}
			field.defaultOption = true;
			return;
		}
		cerr << "Expected `;`, got " << word << " (" << __FILE__ << ':' << __LINE__ << ")\n";
		exit(1);
	}
};

void readOneOf(istream &in, Message &msg) {
	Field &field = msg.fields.emplace_back();
	field.label = Field::Label::ONEOF;
	string word;
	while(in >> word && word.starts_with("//")) getline(in, word);
	field.name = word;
	while(in >> word && word.starts_with("//")) getline(in, word);
	if(word != "{") {
		cerr << "Expected `{`, got " << word << " (" << __FILE__ << ':' << __LINE__ << ")\n";
		exit(1);	
	};
	while(in >> word) {
		if(word.starts_with("//")) {
			getline(in, word);
			continue;
		}
		if(word == "}") break;
		Field::Case &c = field.cases.emplace_back();
		c.type = getType(word, msg);
		if(c.type == Field::Type::MSG) {
			cerr << "Using message type in oneof is not supported yet...\n";
			exit(1);
		}
		if(c.type == Field::Type::STRING || c.type == Field::Type::BYTES)
			field.non_default = true;
		while(in >> word && word.starts_with("//")) getline(in, word);
		c.name = word;
		while(in >> word && word.starts_with("//")) getline(in, word);
		if(word != "=") {
			cerr << "Expected `=`, got " << word << " (" << __FILE__ << ':' << __LINE__ << ")\n";
			exit(1);	
		};
		while(in >> word && word.starts_with("//")) getline(in, word);
		const auto readNumber = [&]() {
			int num = stoi(word);
			if(num < 1 || num >= (1<<29)) {
				cerr << "Bad field number: " << num << endl;
				exit(1);
			}
			c.number = num;
		};
		if(!word.empty() && word.back() == ';') {
			word.pop_back();
			readNumber();
		} else {
			readNumber();
			while(in >> word && word.starts_with("//")) getline(in, word);
			if(word != "=") {
				cerr << "Expected `;`, got " << word << endl;
				exit(1);	
			};
		}
	}
}

void readEnum(istream &in, Message &msg) {
	Enum &enu = msg.enums.emplace_back();
	string word;
	while(in >> word && word.starts_with("//")) getline(in, word);
	enu.name = word;
	while(in >> word && word.starts_with("//")) getline(in, word);
	if(word != "{") {
		cerr << "Expected `{`, got " << word << " (" << __FILE__ << ':' << __LINE__ << ")\n";
		exit(1);	
	};
	while(in >> word) {
		if(word.starts_with("//")) {
			getline(in, word);
			continue;
		}
		if(word == "}") break;
		auto &[name, value] = enu.values.emplace_back();
		name = word;
		in >> word;
		if(word != "=") {
			cerr << "Expected `=`, got " << word << " (" << __FILE__ << ':' << __LINE__ << ")\n";
			exit(1);	
		};
		while(in >> word && word.starts_with("//")) getline(in, word);
		if(!word.empty() && word.back() == ';') {
			word.pop_back();
			value = stoi(word);
		} else {
			value = stoi(word);
			while(in >> word && word.starts_with("//")) getline(in, word);
			if(word == ";") continue;
			cerr << "Expected `;`, got " << word << " (" << __FILE__ << ':' << __LINE__ << ")\n";
			exit(1);
		}
	}
}

template<typename T, size_t N>
constexpr array<T, N> make_array(initializer_list<pair<size_t, T>> init) {
    array<T, N> arr{};
    for(const auto &[i, v] : init) arr[i] = v;
    return arr;
}

constexpr size_t NTE = 10;
constexpr auto typeE2S = make_array<const char*, NTE>({
	{Field::Type::STRING, "std::string"},
	{Field::Type::BYTES, "std::vector<uint8_t>"},
	{Field::Type::BOOL, "bool"},
	{Field::Type::INT32, "int32_t"},
	{Field::Type::INT64, "int64_t"},
	{Field::Type::SINT32, "int32_t"},
	{Field::Type::SINT64, "int64_t"},
	{Field::Type::UINT32, "uint32_t"},
});
constexpr auto typeE2R = make_array<const char*, NTE>({
	{Field::Type::ENUM, "readInt32"},
	{Field::Type::BOOL, "readBool"},
	{Field::Type::INT32, "readInt32"},
	{Field::Type::INT64, "readInt64"},
	{Field::Type::SINT32, "readSint32"},
	{Field::Type::SINT64, "readSint64"},
	{Field::Type::UINT32, "readInt32"},
});
const char* typeString(Field::Type type) {
	if(type >= typeE2S.size() || !typeE2S[type]) {
		cerr << "Not implemented (" << __FILE__ << ':' << __LINE__ << "): " << type << endl;
		exit(1);
	}
	return typeE2S[type];
}
const char* typeRead(Field::Type type) {
	if(type >= typeE2S.size() || !typeE2R[type]) {
		cerr << "Not implemented (" << __FILE__ << ':' << __LINE__ << "): " << type << endl;
		exit(1);
	}
	return typeE2R[type];
}

int main(int argc, char* argv[]) {
	if(argc != 3) {
		cerr << "Usage:\n";
		cerr << ">> " << argv[0] << " `file.proto` `output-folder`\n";
		cerr << "This will produce two files `file.pb.h` and `file.pb.cpp` in the folder `output-folder`\n";
		return 1;
	}
	
	const filesystem::path protoPath = argv[1];
	if(protoPath.extension() != ".proto") {
		cerr << "The argument of this program should be a protobuf file with extension `.proto`\n";
		return 1;
	}

	ifstream file(protoPath);
	string word;
	vector<Message> messages;

	bool comment = false;
	mainLoop:
	while(file >> word) {
		if(comment) {
			if(word.ends_with("*/")) comment = false;
			continue;
		}
		if(word == "message") {
			Message &msg = messages.emplace_back();
			file >> msg.name >> word;
			if(word != "{") {
				cerr << "`message " << msg.name << "` should be followed by `{`\n";
				return 1;
			}
			while(file >> word) {
				if(comment) {
					if(word.ends_with("*/")) comment = false;
					continue;
				}
				if(word == "}") {
					msgNames.insert(msg.name);
					goto mainLoop;
				}
				if(word.starts_with("//")) {
					getline(file, word);
					continue;
				}
				if(word.starts_with("/*")) {
					comment = true;
					continue;
				}
				if(word == "required") readField(file, msg, Field::Label::REQUIRED);
				else if(word == "optional") readField(file, msg, Field::Label::OPTIONAL);
				else if(word == "repeated") readField(file, msg, Field::Label::REPEATED);
				else if(word == "oneof") {
					readOneOf(file, msg);
					msg.non_default |= msg.fields.back().non_default;
				} else if(word == "enum") {
					readEnum(file, msg);
				} else {
					cerr << "Unknown field label: " << word << " (" << __FILE__ << ':' << __LINE__ << ")\n";
					return 1;
				}
			}
			return 1;
		} else if(word.starts_with("//")) {
			getline(file, word);
		} else if(word.starts_with("/*")) {
			comment = true;
		} else {
			cerr << "Unknown word (" << __FILE__ << ':' << __LINE__ << "): " << word << endl;
			return 1;
		}
	}

	const filesystem::path outputDir = argv[2];

	ofstream Hfile(outputDir / (protoPath.stem() += ".pb.h"));
	Hfile << "#include <cstdint>\n";
	Hfile << "#include <string>\n";
	Hfile << "#include <span>\n";
	Hfile << "#include <vector>\n";
	Hfile << "\nnamespace Proto {\n";
	for(const Message &msg : messages) {
		Hfile << "\nstruct " << msg.name << " {\n";
		for(const Enum &e : msg.enums) {
			Hfile << "\tenum " << e.name << " : int {\n";
			for(const auto &[name, value] : e.values) {
				Hfile << "\t\t" << name << " = " << value << ",\n";
			}
			Hfile << "\t};\n";
		}
		if(!msg.enums.empty()) Hfile << '\n';
		for(const Field &f : msg.fields) {
			const auto getTypeString = [&]() {
				if(f.type == Field::Type::MSG || f.type == Field::Type::ENUM)
					return f.msgTypeName.c_str();
				return typeString(f.type);
			};
			switch(f.label) {
			case Field::Label::ONEOF: {
				if(f.non_default) Hfile << "\tunion __" << f.name << "_t {\n";
				else Hfile << "\tunion {\n";
				for(const Field::Case &c : f.cases)
					Hfile << "\t\t" << typeString(c.type) << " " << c.name << ";\n";
				if(f.non_default) {
					Hfile << "\t\t__" << f.name << "_t() {}\n";
					Hfile << "\t\t~__" << f.name << "_t() {}\n";
				}
				Hfile << "\t} " << f.name << ";\n";
				Hfile << "\tenum {\n";
				string FNAME = f.name, CNAME;
				for(char &c : FNAME) c = toupper(c);
				for(const Field::Case &c : f.cases) {
					CNAME = c.name;
					for(char &c : CNAME) c = toupper(c);
					Hfile << "\t\t" << FNAME << '_' << CNAME << ",\n";
				}
				Hfile << "\t\t_" << FNAME << "_NONE\n";
				Hfile << "\t} _" << f.name << "_choice = _" << FNAME << "_NONE;\n";
				break;
			}
			case Field::Label::REPEATED:
			case Field::Label::PACKED:
				Hfile << "\tstd::vector<" << getTypeString() << "> " << f.name << ";\n";
				break;
			case Field::Label::REQUIRED:
			case Field::Label::OPTIONAL:
				Hfile << "\t" << getTypeString() << " " << f.name;
				if(f.label == Field::Label::OPTIONAL) {
					switch(f.type) {
					case Field::Type::MSG:
					case Field::Type::STRING:
					case Field::Type::BYTES:
						break;
					case Field::Type::BOOL:
						Hfile << " = false";
						break;
					case Field::Type::INT32:
						if(f.defaultOption) Hfile << " = " << f.defaultValue.i32;
						else Hfile << " = 0";
						break;
					case Field::Type::INT64:
					case Field::Type::SINT64:
						if(f.defaultOption) Hfile << " = " << f.defaultValue.i64;
						else Hfile << " = 0";
						break;
					case Field::Type::UINT32:
						Hfile << " = 0";
						break;
					default:
						cerr << "Not implemented (" << __FILE__ << ':' << __LINE__ << "): " << f.type << endl;
					}
					Hfile << ";\n\tbool _has_" << f.name << " = false";
				}
				Hfile << ";\n";
				break;
			default:
				cerr << "Not implemented (" << __FILE__ << ':' << __LINE__ << "): " << f.type << endl;
			}
		}
		Hfile << "\n\t" << msg.name << "() = default;\n";
		Hfile << "\t" << msg.name << "(const std::span<const uint8_t> &wire);\n";
		if(msg.non_default) Hfile << "\t~" << msg.name << "();\n";
		Hfile << "};\n";
	}
	Hfile << "\n}\n";
	Hfile.close();
	
	ofstream Cfile(outputDir / (protoPath.stem() += ".pb.cpp"));
	Cfile << "#include " << (protoPath.stem() += ".pb.h") << "\n\n";
	Cfile << "#include \"proto_common.h\"\n\n";
	Cfile << "#include <algorithm>\n";
	Cfile << "#include <iostream>\n";
	Cfile << "\nusing namespace std;\n";
	Cfile << "\nnamespace Proto {\n\n";

	const auto readField = [&](const string &fname, const Field::Type ftype, const string &ftypeName, const string &msgname, const Field::Label label) {
		switch(ftype) {
		case Field::Type::MSG:
		case Field::Type::STRING:
		case Field::Type::BYTES:
			Cfile << "\t\t\tif(wire_type != 2u) {\n";
			Cfile << "\t\t\t\tcerr << \"Bad wire type while reading " << msgname << '.' << fname
				<< " (got \" << wire_type << \" instead of 2...\\n\";\n";
			Cfile << "\t\t\t\texit(1);\n";
			Cfile << "\t\t\t}\n";
			Cfile << "\t\t\tconst uint32_t len = readInt32(it);\n";
			Cfile << "\t\t\tif(it + len > end_it) {\n";
			Cfile << "\t\t\t\tcerr << \"The " <<
				(ftype == Field::Type::MSG ? ftypeName.c_str() :
					(ftype == Field::Type::STRING ? "string" : "bytes")
				) << " stored in " << msgname << '.' << fname << " is too long...\\n\";\n";
			Cfile << "\t\t\t\texit(1);\n";
			Cfile << "\t\t\t}\n";
			if(ftype == Field::Type::MSG) {
				if(label == Field::Label::REPEATED)
					Cfile << "\t\t\t" << fname << ".emplace_back(span(it, len));\n";
				else
					Cfile << "\t\t\t" << fname << " = " << ftypeName << "(span(it, len));\n";
			} else if(label == Field::Label::REPEATED) {
				Cfile << "\t\t\t" << fname << ".emplace_back(it, it + len);\n";
			} else if(label == Field::Label::ONEOF) {
				Cfile << "\t\t\tnew(&" << fname << ") " << typeString(ftype) << "(it, it + len);\n";
			} else {
				Cfile << "\t\t\t" << fname << ".assign(it, it + len);\n";
			}
			Cfile << "\t\t\tit += len;\n";
			break;
		case Field::Type::ENUM:
		case Field::Type::BOOL:
		case Field::Type::INT32:
		case Field::Type::INT64:
		case Field::Type::SINT32:
		case Field::Type::SINT64:
		case Field::Type::UINT32:
			if(label == Field::Label::PACKED) {
				Cfile << "\t\t\tif(wire_type != 2u) {\n";
				Cfile << "\t\t\t\tcerr << \"Bad wire type while reading " << msgname << '.' << fname
					<< " (got \" << wire_type << \" instead of 2...\\n\";\n";
				Cfile << "\t\t\t\texit(1);\n";
				Cfile << "\t\t\t}\n";
				Cfile << "\t\t\tconst uint32_t len = readInt32(it);\n";
				Cfile << "\t\t\tconst uint8_t *it2 = it + len;\n";
				Cfile << "\t\t\tif(it2 > end_it) {\n";
				Cfile << "\t\t\t\tcerr << \"The value stored in " << msgname << '.' << fname << " is too long...\\n\";\n";
				Cfile << "\t\t\t\texit(1);\n";
				Cfile << "\t\t\t}\n";
				Cfile << "\t\t\twhile(it != it2) {\n";
				Cfile << "\t\t\t\t" << fname << ".push_back(";
					if(ftype == Field::Type::ENUM) Cfile << '(' << ftypeName << ") ";
					Cfile << typeRead(ftype) << "(it));\n";
				Cfile << "\t\t\t}\n";
			} else {
				Cfile << "\t\t\tif(wire_type != 0u) {\n";
				Cfile << "\t\t\t\tcerr << \"Bad wire type while reading " << msgname << '.' << fname
					<< " (got \" << wire_type << \" instead of 0...\\n\";\n";
				Cfile << "\t\t\t\texit(1);\n";
				Cfile << "\t\t\t}\n";
				if(label == Field::Label::REPEATED) {
					Cfile << "\t\t\t" << fname << ".push_back(";
						if(ftype == Field::Type::ENUM) Cfile << '(' << ftypeName << ") ";
						Cfile << typeRead(ftype) << "(it));\n";
				} else
					Cfile << "\t\t\t" << fname << " = " << typeRead(ftype) << "(it);\n";
			}
			break;
		default:
			cerr << "Not implemented (" << __FILE__ << ':' << __LINE__ << "): " << ftype << endl;
		}
		Cfile << "\t\t\tbreak;\n";
		Cfile << "\t\t}\n";
	};

	for(const Message &msg : messages) {
		// Destroy
		for(const Field &f : msg.fields) if(f.non_default) {
			Cfile << "void destroy_" << msg.name << "_" << f.name << "(" << msg.name << " &x) {\n";
			string FNAME = f.name, CNAME;
			for(char &c : FNAME) c = toupper(c);
			Cfile << "\tswitch(x._" << f.name << "_choice) {\n";
			for(const Field::Case &c : f.cases) {
				CNAME = c.name;
				for(char &c : CNAME) c = toupper(c);
				switch(c.type) {
				case Field::Type::STRING:
				case Field::Type::BYTES:
					Cfile << "\tcase " << msg.name << "::" << FNAME << "_" << CNAME << ":\n";
					Cfile << "\t\tx." << f.name << "." << c.name << ".~" << (typeString(c.type)+5) << "();\n";
					Cfile << "\t\tbreak;\n";
					break;
				case Field::Type::INT32:
					break;
				default:
					cerr << "Not implemented (3): " << c.type << endl;
				}
			}
			Cfile << "\tdefault:\n";
			Cfile << "\t\tbreak;\n";
			Cfile << "\t}\n";
			Cfile << "\tx._" << f.name << "_choice = " << msg.name << "::_" << FNAME << "_NONE;\n";
			Cfile << "}\n\n";
		};

		// Constructor
		Cfile << msg.name << "::" << msg.name << "(const span<const uint8_t> &wire) {\n";
		Cfile << "\tconst uint8_t* it = wire.data();\n";
		Cfile << "\tconst uint8_t* const end_it = it + wire.size();\n";
		for(const Field &f : msg.fields) if(f.label == Field::Label::REQUIRED)
			Cfile << "\tbool __required__" << f.name << " = false;\n";
		Cfile << "\twhile(it != end_it) {\n";
		Cfile << "\t\tconst uint32_t key = readInt32(it);\n";
		Cfile << "\t\tconst uint32_t field_number = key >> 3;\n";
		Cfile << "\t\tconst uint32_t wire_type = key & 0x7u;\n";
		Cfile << "\t\tswitch(field_number) {\n";
		for(const Field &f : msg.fields) {
			if(f.label == Field::Label::ONEOF) {
				string FNAME = f.name, CNAME;
				for(char &c : FNAME) c = toupper(c);
				for(const Field::Case &c : f.cases) {
					const string var = f.name + "." + c.name;
					Cfile << "\t\tcase " << c.number << ": { // field: " << var << '\n';
					CNAME = c.name;
					for(char &c : CNAME) c = toupper(c);
					Cfile << "\t\t\tdestroy_" << msg.name << "_" << f.name << "(*this);\n";
					Cfile << "\t\t\t_" << f.name << "_choice = " << FNAME << "_" << CNAME << ";\n";
					readField(var, c.type, "", msg.name, f.label);
				}
			} else {
				Cfile << "\t\tcase " << f.number << ": { // field: " << f.name << '\n';
				if(f.label == Field::Label::REQUIRED)
					Cfile << "\t\t\t__required__" << f.name << " = true;\n";
				else if(f.label == Field::Label::OPTIONAL)
					Cfile << "\t\t\t_has_" << f.name << " = true;\n";
				readField(f.name, f.type, f.msgTypeName, msg.name, f.label);
			}
		}
		Cfile << "\t\tdefault:\n";
		Cfile << "\t\t\tcerr << \"Bad field number (\" << field_number << \") while reading " << msg.name << " wire\\n\";\n";
		Cfile << "\t\t\texit(1);\n";
		Cfile << "\t\t}\n";
		Cfile << "\t}\n";
		for(const Field &f : msg.fields) if(f.label == Field::Label::REQUIRED) {
			Cfile << "\tif(!__required__" << f.name << ") {\n";
			Cfile << "\t\tcerr << \"Field " << msg.name << '.' << f.name << " required and not present in wire...\";\n";
			Cfile << "\t\texit(1);\n";
			Cfile << "\t}\n";
		}
		Cfile << "}\n\n";

		if(!msg.non_default) continue;
		// Destructor
		Cfile << msg.name << "::~" << msg.name << "() {\n";
		for(const Field &f : msg.fields) if(f.non_default)
			Cfile << "\tdestroy_" << msg.name << "_" << f.name << "(*this);\n";
		Cfile << "}\n\n";
	}
	Cfile << "}\n";
	Cfile.close();

	return 0;
}