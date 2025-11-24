// Copyright (C) 2025, Coudert--Osmont Yoann
// SPDX-License-Identifier: AGPL-3.0-or-later
// See <https://www.gnu.org/licenses/>

#include <algorithm>
#include <concepts>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <ranges>
#include <regex>
#include <sstream>
#include <unordered_map>
#include <vector>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include "glad/gl.h"

using namespace std;

struct Attribute {
	string name;
	GLuint index;
	GLuint type;
};

struct Uniform {
	string name;
	GLuint index;
	GLuint type;
};

struct Buffer {
	string name;
	GLuint binding;
};

struct UBO {
	string name;
	GLuint binding;
};

struct Prog {
	string name, vertName, fragName;
	vector<Attribute> attributes;
	vector<Uniform> uniforms;
	vector<Buffer> buffers;
	vector<UBO> ubos;
	bool hasAttribsStruct() const {
		return !attributes.empty() && attributes.back().index+1 == (GLuint) attributes.size();
	}
};

static filesystem::path shaderDir;
static GLchar infoLog[1024];

static void compileShaderFile(GLuint shader, const char* fileName) {
	ifstream file(fileName, ios::ate);
	if(!file) {
		cerr << "Failed to open shader file: " << fileName << '\n';
		exit(1);
	}
	const GLint size = (GLint) file.tellg();
	char* src = new char[size];
	file.seekg(0);
	file.read(src, size);
	file.close();
	const regex includeRegex(R"(#include\s+[\"<]/(.*)[\">])");
	cmatch match;
	const char* search_src = src;
	const char* search_src_end = search_src + size;
	while(regex_search(search_src, search_src_end, match, includeRegex)) {
		if(!glIsNamedStringARB(match[1].length()+1, match[1].first-1)) {
			ifstream includeFile(shaderDir / match[1].str(), ios::ate);
			if(!includeFile) {
				cerr << "Failed to open shader include file: " << (shaderDir / match[1].str()) << '\n';
				exit(1);
			}
			const GLint includeSize = (GLint) includeFile.tellg();
			char* includeSrc = new char[includeSize];
			includeFile.seekg(0);
			includeFile.read(includeSrc, includeSize);
			includeFile.close();
			glNamedStringARB(GL_SHADER_INCLUDE_ARB, match[1].length()+1, match[1].first-1, includeSize, includeSrc);
			delete[] includeSrc;
		}
		search_src = match.suffix().first;
	}
	glShaderSource(shader, 1, &src, &size);
	delete[] src;
	glCompileShaderIncludeARB(shader, 0, nullptr, nullptr);
	GLint succes;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &succes);
	if(!succes) {
		glGetShaderInfoLog(shader, sizeof(infoLog), nullptr, infoLog);
		cerr << "Failed to compile shader: " << fileName << "\n" << infoLog;
		exit(1);
	}
}

template<typename T>
concept RessourceType = requires(T x) {
	{ x.name } -> same_as<string&>;
};

template<GLenum inteface, GLenum... properties, RessourceType Ressource, invocable<Ressource&, GLint*> Fun>
void getRessources(GLuint prog, vector<Ressource> &ressources, const Fun &f) {
	GLint num = 0;
	glGetProgramInterfaceiv(prog, inteface, GL_ACTIVE_RESOURCES, &num);
	ressources.resize(num);
	for(GLint i = 0; i < num; ++i) {
		const GLenum props[] {GL_NAME_LENGTH, properties...};
		GLint vals[std::size(props)];
		glGetProgramResourceiv(prog, inteface, i, std::size(props), props, std::size(props), nullptr, vals);
		Ressource &r = ressources[i];
		r.name.resize(vals[0]);
		if(!r.name.empty()) {
			glGetProgramResourceName(prog, inteface, i, r.name.size(), nullptr, r.name.data());
			r.name.pop_back();
		}
		f(r, vals);
	}
}

static const char* getTypeFullName(GLuint type) {
	switch(type) {
	case GL_FLOAT_VEC2:
		return "vec2f";
	case GL_FLOAT_VEC3:
		return "vec3f";
	default:
		cerr << "Unknown type: " << hex << type << dec << " (" << __FILE__ << ':' << __LINE__ << ")\n";
		exit(1);
	}
};

static const char* getTypeName(GLuint type) {
	switch(type) {
	case GL_FLOAT_VEC2:
	case GL_FLOAT_VEC3:
		return "GL_FLOAT";
	default:
		cerr << "Unknown type: " << hex << type << dec << " (" << __FILE__ << ':' << __LINE__ << ")\n";
		exit(1);
	}
};

static GLuint getTypeSize(GLuint type) {
	switch(type) {
	case GL_FLOAT_VEC2:
		return 2;
	case GL_FLOAT_VEC3:
		return 3;
	default:
		cerr << "Unknown type: " << hex << type << dec << " (" << __FILE__ << ':' << __LINE__ << ")\n";
		exit(1);
	}
};

int main(int argc, char* argv[]) {
	if(argc != 4) {
		cerr << "Usage:\n";
		cerr << ">> " << argv[0] << " `list.txt` `shaders-folder` `output-folder`\n";
		cerr << "This will produce two files `programs.h` and `programs.cpp` in the folder `output-folder`\n";
		return 1;
	}

	vector<Prog> progs;
	unordered_map<string, GLuint> vertShaders, fragShaders;
	
	// Read list
	ifstream listFile(argv[1]);
	if(!listFile) {
		cerr << "Can't open file: " << argv[1] << endl;
		exit(1);
	}
	string progName, vertName, fragName;
	while(listFile >> progName >> vertName >> fragName) {
		vertShaders.emplace(vertName, 0);
		fragShaders.emplace(fragName, 0);
		Prog &prog = progs.emplace_back();
		prog.name = move(progName);
		prog.vertName = move(vertName);
		prog.fragName = move(fragName);
	}
	listFile.close();

	if(!glfwInit()) {
		cerr << "Failed to init glfw!\n";
		exit(1);
	}
	glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
	GLFWwindow *window = glfwCreateWindow(1, 1, "Program Generator", nullptr, nullptr);
	glfwMakeContextCurrent(window);
	gladLoadGL(glfwGetProcAddress);

	// Search for attributes and uniforms
	shaderDir = argv[2];
	for(auto &[name, shader] : vertShaders) {
		shader = glCreateShader(GL_VERTEX_SHADER);
		compileShaderFile(shader, (shaderDir / (name + ".vert")).c_str());
	}
	for(auto &[name, shader] : fragShaders) {
		shader = glCreateShader(GL_FRAGMENT_SHADER);
		compileShaderFile(shader, (shaderDir / (name + ".frag")).c_str());
	}
	for(Prog &prog : progs) {
		const GLuint prg = glCreateProgram();
		glAttachShader(prg, vertShaders[prog.vertName]);
		glAttachShader(prg, fragShaders[prog.fragName]);
		glLinkProgram(prg);
		GLint succes;
		glGetProgramiv(prg, GL_LINK_STATUS, &succes);
		if(!succes) {
			glGetProgramInfoLog(prg, sizeof(infoLog), nullptr, infoLog);
			cerr << "Failed to link program: \n" << infoLog;
			exit(1);
		}
		getRessources<GL_PROGRAM_INPUT, GL_LOCATION, GL_TYPE>(prg, prog.attributes, [](Attribute &a, GLint* vals) {
			a.index = vals[1];
			a.type = vals[2];
		});
		for(int i = 0; i < (int) prog.attributes.size();) {
			if(prog.attributes[i].name == "gl_VertexID") {
				prog.attributes[i] = move(prog.attributes.back());
				prog.attributes.pop_back();
			} else ++i;
		}
		ranges::sort(prog.attributes, less<GLuint>{}, [&](const Attribute &a) { return a.index; });
		getRessources<GL_UNIFORM, GL_LOCATION, GL_TYPE>(prg, prog.uniforms, [](Uniform &u, GLint* vals) {
			u.index = vals[1];
			u.type = vals[2];
		});
		for(int i = 0; i < (int) prog.uniforms.size();) {
			if(prog.uniforms[i].index == (GLuint)-1) {
				prog.uniforms[i] = move(prog.uniforms.back());
				prog.uniforms.pop_back();
			} else ++i;
		}
		getRessources<GL_SHADER_STORAGE_BLOCK, GL_BUFFER_BINDING>(prg, prog.buffers, [](Buffer &b, GLint* vals) {
			b.binding = vals[1];
		});
		// TODO: create a C++ UBO struct
		getRessources<GL_UNIFORM_BLOCK, GL_BUFFER_BINDING>(prg, prog.ubos, [](UBO &u, GLint* vals) {
			u.binding = vals[1];
		});
		glDeleteProgram(prg);
	}
	for(const GLuint &shader : vertShaders | views::elements<1>) glDeleteShader(shader);
	for(const GLuint &shader : fragShaders | views::elements<1>) glDeleteShader(shader);

	const filesystem::path outputDir = argv[3];

	// Generate header
	ofstream Hfile(outputDir / "programs.h");
	Hfile << R"lim(#include "glad/gl.h"
#include "vec.h"

struct Programs {
	void init();

	struct Program {
		inline void use() { glUseProgram(prog); }
	protected:
		friend Programs;
		GLuint prog;
		void init(GLuint vertexShader, GLuint fragmentShader);

		template<GLuint attribIndex, GLint size, GLenum type>
		void __bind(GLuint VAO, GLuint bindingIndex, GLuint offset) const {
			glEnableVertexArrayAttrib(VAO, attribIndex);
			glVertexArrayAttribBinding(VAO, attribIndex, bindingIndex);
			glVertexArrayAttribFormat(VAO, attribIndex, size, type, GL_FALSE, offset);
		}
	};
)lim";

	for(const Prog &prog : progs) {	
		string typeName = prog.name;
		typeName[0] = toupper(typeName[0]);
		Hfile << "\n\tstruct " << typeName << " : Program {\n";
		if(prog.hasAttribsStruct()) {
			Hfile << "\t\tstruct Attribs {\n";
			for(const Attribute &a : prog.attributes)
				Hfile << "\t\t\t" << getTypeFullName(a.type) << ' ' << a.name << ";\n";
			Hfile << "\t\t};\n\n";
			Hfile << "\t\tvoid canonical_bind(GLuint VAO, GLuint bindingIndex) const;\n";
		}
		for(const Attribute &a : prog.attributes) {
			Hfile << "\t\tinline void bind_" << a.name << "(GLuint VAO, GLuint bindingIndex, GLuint offset) const {\n";
			Hfile << "\t\t\t__bind<" << a.index << ", " << getTypeSize(a.type) << ", " << getTypeName(a.type) << ">(VAO, bindingIndex, offset);\n";
			Hfile << "\t\t}\n";
		}
		for(const Buffer &b : prog.buffers) {
			Hfile << "\t\tinline void bind_" << b.name << "(GLuint SSBO) const {\n";
			Hfile << "\t\t\tglBindBufferBase(GL_SHADER_STORAGE_BUFFER, " << b.binding << ", SSBO);\n";
			Hfile << "\t\t}\n";
			Hfile << "\t\tinline void bind_" << b.name << "_range(GLuint SSBO, GLintptr offset, GLsizeiptr size) const {\n";
			Hfile << "\t\t\tglBindBufferRange(GL_SHADER_STORAGE_BUFFER, " << b.binding << ", SSBO, offset, size);\n";
			Hfile << "\t\t}\n";
		}
		for(const UBO &u : prog.ubos) {
			Hfile << "\t\tinline void bind_" << u.name << "(GLuint UBO) const {\n";
			Hfile << "\t\t\tglBindBufferBase(GL_UNIFORM_BUFFER, " << u.binding << ", UBO);\n";
			Hfile << "\t\t}\n";
		}
		for(const Uniform &u : prog.uniforms) {
			Hfile << "\t\tinline void set_" << u.name << '(';
			switch(u.type) {
			case GL_FLOAT_VEC2:
				Hfile << "GLfloat x, GLfloat y) { glUniform2f(" << u.index << ", x, y); }\n";
				break;
			case GL_FLOAT_VEC3:
				Hfile << "GLfloat x, GLfloat y, GLfloat z) { glUniform3f(" << u.index << ", x, y, z); }\n";
				break;
			case GL_SAMPLER_2D:
				Hfile << "GLint i) { glUniform1i(" << u.index << ", i); }\n";
				break;
			default:
				cerr << "Unknown uniform type: " << u.name << " : " << hex << u.type << dec << " (" << __FILE__ << ':' << __LINE__ << ")\n";
				exit(1);
			}
		}
		Hfile << "\t} " << prog.name << ";\n";
	}
	Hfile << "\n};\n";
	Hfile.close();

	// Generate CPP
	ofstream Cfile(outputDir / "programs.cpp");
	Cfile << R"lim(#include "programs.h"

#include <iostream>
#include <fstream>
#include <regex>

using namespace std;

static GLchar infoLog[1024];

static void compileShaderFile(GLuint shader, const char* fileName) {
	ifstream file(fileName, ios::ate);
	if(!file) {
		cerr << "Failed to open shader file: " << fileName << '\n';
		exit(1);
	}
	const GLint size = (GLint) file.tellg();
	char* src = new char[size];
	file.seekg(0);
	file.read(src, size);
	file.close();
	const regex includeRegex(R"(#include\s+[\"<]/(.*)[\">])");
	cmatch match;
	const char* search_src = src;
	const char* search_src_end = search_src + size;
	while(regex_search(search_src, search_src_end, match, includeRegex)) {
		if(!glIsNamedStringARB(match[1].length()+1, match[1].first-1)) {
			ifstream includeFile(SHADER_DIR "/" + match[1].str(), ios::ate);
			if(!includeFile) {
				cerr << "Failed to open shader include file: " << (SHADER_DIR "/" + match[1].str()) << '\n';
				exit(1);
			}
			const GLint includeSize = (GLint) includeFile.tellg();
			char* includeSrc = new char[includeSize];
			includeFile.seekg(0);
			includeFile.read(includeSrc, includeSize);
			includeFile.close();
			glNamedStringARB(GL_SHADER_INCLUDE_ARB, match[1].length()+1, match[1].first-1, includeSize, includeSrc);
			delete[] includeSrc;
		}
		search_src = match.suffix().first;
	}
	glShaderSource(shader, 1, &src, &size);
	delete[] src;
	glCompileShaderIncludeARB(shader, 0, nullptr, nullptr);
	GLint succes;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &succes);
	if(!succes) {
		glGetShaderInfoLog(shader, sizeof(infoLog), nullptr, infoLog);
		cerr << "Failed to compile shader: " << fileName << "\n" << infoLog;
		exit(1);
	}
}

void Programs::Program::init(GLuint vertexShader, GLuint fragmentShader) {
	prog = glCreateProgram();
	glAttachShader(prog, vertexShader);
	glAttachShader(prog, fragmentShader);
	glLinkProgram(prog);
	GLint succes;
	glGetProgramiv(prog, GL_LINK_STATUS, &succes);
	if(!succes) {
		glGetProgramInfoLog(prog, sizeof(infoLog), nullptr, infoLog);
		cerr << "Failed to link program: \n" << infoLog;
		exit(1);
	}
}
)lim";

	Cfile << "\nvoid Programs::init() {\n";
	for(const auto &name : vertShaders | views::elements<0>) {
		Cfile << "\tconst GLuint vert_" << name << " = glCreateShader(GL_VERTEX_SHADER);\n";
		Cfile << "\tcompileShaderFile(vert_" << name << ", SHADER_DIR \"/" << name << ".vert\");\n";
	}
	for(const auto &name : fragShaders | views::elements<0>) {
		Cfile << "\tconst GLuint frag_" << name << " = glCreateShader(GL_FRAGMENT_SHADER);\n";
		Cfile << "\tcompileShaderFile(frag_" << name << ", SHADER_DIR \"/" << name << ".frag\");\n";
	}
	for(const Prog &prog : progs)
		Cfile << "\t" << prog.name << ".init(vert_" << prog.vertName << ", frag_" << prog.fragName << ");\n";
	for(const auto &name : vertShaders | views::elements<0>)
		Cfile << "\tglDeleteShader(vert_" << name << ");\n";
	for(const auto &name : fragShaders | views::elements<0>)
		Cfile << "\tglDeleteShader(frag_" << name << ");\n";
	Cfile << "}\n";

	for(const Prog &prog : progs) {	
		string typeName = prog.name;
		typeName[0] = toupper(typeName[0]);
		if(prog.hasAttribsStruct()) {
			Cfile << "\nvoid Programs::" << typeName << "::canonical_bind(GLuint VAO, GLuint bindingIndex) const {\n";
			for(const Attribute &a : prog.attributes)
				Cfile << "\tbind_" << a.name << "(VAO, bindingIndex, offsetof(Attribs, " << a.name << "));\n";
			Cfile << "}\n";
		}
	}

	Cfile.close();

	return 0;
}