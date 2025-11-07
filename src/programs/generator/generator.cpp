// Copyright (C) 2025, Coudert--Osmont Yoann
// SPDX-License-Identifier: AGPL-3.0-or-later
// See <https://www.gnu.org/licenses/>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <ranges>
#include <sstream>
#include <unordered_map>
#include <vector>

using namespace std;

enum Type {
	VEC2,
	VEC3,
	SAMPLER2D
};

struct Prog {
	string name, vertName, fragName;
	vector<string> attributes;
	vector<pair<Type, string>> uniforms;
};

Type getType(const string &s) {
	if(s == "vec2") return Type::VEC2;
	if(s == "vec3") return Type::VEC3;
	if(s == "sampler2D") return Type::SAMPLER2D;
	cerr << "Unknown type: " << s << endl;
	exit(1);
}

int main(int argc, char* argv[]) {
	if(argc != 4) {
		cerr << "Usage:\n";
		cerr << ">> " << argv[0] << " `list.txt` `shaders-folder` `output-folder`\n";
		cerr << "This will produce two files `programs.h` and `programs.cpp` in the folder `output-folder`\n";
		return 1;
	}

	vector<Prog> progs;
	unordered_map<string, vector<int>> vertNames, fragNames;
	
	// Read list
	ifstream listFile(argv[1]);
	if(!listFile) {
		cerr << "Can't open file: " << argv[1] << endl;
		exit(1);
	}
	string progName, vertName, fragName;
	while(listFile >> progName >> vertName >> fragName) {
		vertNames[vertName].push_back(progs.size());
		fragNames[fragName].push_back(progs.size());
		Prog &prog = progs.emplace_back();
		prog.name = move(progName);
		prog.vertName = move(vertName);
		prog.fragName = move(fragName);
	}
	listFile.close();

	// Search for attributes and uniforms
	const filesystem::path shaderDir = argv[2];
	for(const auto &[shaderNames, ext] : {make_pair(&vertNames, ".vert"), {&fragNames, ".frag"}}) {
		const bool readAtt = ext[1] == 'v';
		for(const auto &[name, inds] : *shaderNames) {
			ifstream shader(shaderDir / (name + ext));
			if(!shader) {
				cerr << "Can't find vertex shader " << vertName << endl;
				exit(1);
			}
			string line;
			while(getline(shader, line)) {
				istringstream iss(line);
				string word;
				iss >> word;
				if(word == "uniform") {
					iss >> word;
					Type t = getType(word);
					iss >> word;
					if(word.empty()) continue;
					if(word.back() == ';') {
						word.pop_back();
						if(word.empty()) continue;
					}
					for(int i : inds) progs[i].uniforms.emplace_back(t, word);
				} else if(readAtt && word == "layout") {
					iss >> word;
					if(word != "(location") continue;
					iss >> word;
					if(word != "=") continue;
					iss >> word;
					if(word.empty() || word.back() != ')') continue;
					iss >> word;
					if(word != "in") continue;
					iss >> word;
					getType(word);
					iss >> word;
					if(word.empty()) continue;
					if(word.back() == ';') {
						word.pop_back();
						if(word.empty()) continue;
					}
					for(int i : inds) progs[i].attributes.emplace_back(word);
				}
			}
		}
	}

	const filesystem::path outputDir = argv[3];

	// Generate header
	ofstream Hfile(outputDir / "programs.h");
	Hfile << "#include \"glad/gl.h\"\n";
	Hfile << "\nstruct Programs {\n";
	Hfile << "\tvoid init();\n";
	Hfile << "\n\tstruct Program {\n";
	Hfile << "\t\tinline void use() { glUseProgram(prog); }\n";
	Hfile << "\tprotected:\n";
	Hfile << "\t\tfriend Programs;\n";
	Hfile << "\t\tGLuint prog;\n";
	Hfile << "\t\tvoid init(GLuint vertexShader, GLuint fragmentShader);\n";
	Hfile << "\t};\n";
	for(const Prog &prog : progs) {
		Hfile << "\n\tstruct : Program {\n";
		for(const auto &name : prog.attributes) {
			Hfile << "\t\tinline GLint get_" << name << "() const { return " << name << "; }\n";
		}
		for(const auto &[t, name] : prog.uniforms) {
			Hfile << "\t\tinline void set_" << name << '(';
			switch(t) {
			case Type::VEC2:
				Hfile << "GLfloat x, GLfloat y) { glUniform2f(" << name << ", x, y); }\n";
				break;
			case Type::VEC3:
				Hfile << "GLfloat x, GLfloat y, GLfloat z) { glUniform3f(" << name << ", x, y, z); }\n";
				break;
			case Type::SAMPLER2D:
				Hfile << "GLint i) { glUniform1i(" << name << ", i); }\n";
				break;
			default:
				cerr << "Not implemented (0)" << endl;
				exit(1);
			}
		}
		Hfile << "\tprotected:\n";
		Hfile << "\t\tfriend Programs;\n";
		if(!prog.attributes.empty()) {
			Hfile << "\t\tGLint";
			for(int i = 0; i < (int) prog.attributes.size(); ++i)
				Hfile << (i ? ", " : " ") << prog.attributes[i];
			Hfile << ";\n";
		}
		if(!prog.uniforms.empty()) {
			Hfile << "\t\tGLint";
			for(int i = 0; i < (int) prog.uniforms.size(); ++i)
				Hfile << (i ? ", " : " ") << prog.uniforms[i].second;
			Hfile << ";\n";
		}
		Hfile << "\t} " << prog.name << ";\n";
	}
	Hfile << "\n};\n";
	Hfile.close();

	// Generate CPP
	ofstream Cfile(outputDir / "programs.cpp");
	Cfile << "#include \"programs.h\"\n\n";
	Cfile << "#include <iostream>\n\n";
	Cfile << "#include <fstream>\n\n";
	Cfile << "using namespace std;\n\n";
	Cfile << "static GLchar infoLog[1024];\n";

	Cfile << "\nstatic void compileShaderFile(GLuint shader, const char* fileName) {\n";
	Cfile << "\tifstream file(fileName, ios::ate);\n";
	Cfile << "\tif(!file) {\n";
	Cfile << "\t\tcerr << \"Failed to open shader file: \" << fileName << '\\n';\n";
	Cfile << "\t\texit(1);\n";
	Cfile << "\t}\n";
	Cfile << "\tconst GLint size = (GLint) file.tellg();\n";
	Cfile << "\tchar* src = new char[size]; // delete[] ???\n";
	Cfile << "\tfile.seekg(0);\n";
	Cfile << "\tfile.read(src, size);\n";
	Cfile << "\tfile.close();\n";
	Cfile << "\tglShaderSource(shader, 1, &src, &size);\n";
	Cfile << "\tglCompileShader(shader);\n";
	Cfile << "\tGLint succes;\n";
	Cfile << "\tglGetShaderiv(shader, GL_COMPILE_STATUS, &succes);\n";
	Cfile << "\tif(!succes) {\n";
	Cfile << "\t\tglGetShaderInfoLog(shader, sizeof(infoLog), nullptr, infoLog);\n";
	Cfile << "\t\tcerr << \"Failed to compile shader: \" << fileName << \"\\n\" << infoLog;\n";
	Cfile << "\t\texit(1);\n";
	Cfile << "\t}\n";
	Cfile << "}\n";

	Cfile << "\nvoid Programs::Program::init(GLuint vertexShader, GLuint fragmentShader) {\n";
	Cfile << "\tprog = glCreateProgram();\n";
	Cfile << "\tglAttachShader(prog, vertexShader);\n";
	Cfile << "\tglAttachShader(prog, fragmentShader);\n";
	Cfile << "\tglLinkProgram(prog);\n";
	Cfile << "\tGLint succes;\n";
	Cfile << "\tglGetProgramiv(prog, GL_LINK_STATUS, &succes);\n";
	Cfile << "\tif(!succes) {\n";
	Cfile << "\t\tglGetProgramInfoLog(prog, sizeof(infoLog), nullptr, infoLog);\n";
	Cfile << "\t\tcerr << \"Failed to link program: \\n\" << infoLog;\n";
	Cfile << "\t\texit(1);\n";
	Cfile << "\t}\n";
	Cfile << "}\n";

	Cfile << "\nvoid Programs::init() {\n";
	for(const auto &name : vertNames | views::elements<0>) {
		Cfile << "\tconst GLuint vert_" << name << " = glCreateShader(GL_VERTEX_SHADER);\n";
		Cfile << "\tcompileShaderFile(vert_" << name << ", SHADER_DIR \"/" << name << ".vert\");\n";
	}
	for(const auto &name : fragNames | views::elements<0>) {
		Cfile << "\tconst GLuint frag_" << name << " = glCreateShader(GL_FRAGMENT_SHADER);\n";
		Cfile << "\tcompileShaderFile(frag_" << name << ", SHADER_DIR \"/" << name << ".frag\");\n";
	}
	for(const Prog &prog : progs)
		Cfile << "\t" << prog.name << ".init(vert_" << prog.vertName << ", frag_" << prog.fragName << ");\n";
	for(const auto &name : vertNames | views::elements<0>)
		Cfile << "\tglDeleteShader(vert_" << name << ");\n";
	for(const auto &name : fragNames | views::elements<0>)
		Cfile << "\tglDeleteShader(frag_" << name << ");\n";
	for(const Prog &prog : progs) {
		for(const auto &name : prog.attributes)
			Cfile << "\t" << prog.name << '.' << name << " = glGetAttribLocation(" << prog.name << ".prog, \"" << name << "\");\n";
		for(const auto &name : prog.uniforms | views::elements<1>)
			Cfile << "\t" << prog.name << '.' << name << " = glGetUniformLocation(" << prog.name << ".prog, \"" << name << "\");\n";
	}
	Cfile << "}\n";
	Cfile.close();

	return 0;
}