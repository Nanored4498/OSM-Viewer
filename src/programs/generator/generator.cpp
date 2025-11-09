// Copyright (C) 2025, Coudert--Osmont Yoann
// SPDX-License-Identifier: AGPL-3.0-or-later
// See <https://www.gnu.org/licenses/>

#include <concepts>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <ranges>
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

struct Prog {
	string name, vertName, fragName;
	vector<Attribute> attributes;
	vector<Uniform> uniforms;
	vector<Buffer> buffers;
};

static GLchar infoLog[1024];

static void compileShaderFile(GLuint shader, const char* fileName) {
	ifstream file(fileName, ios::ate);
	if(!file) {
		cerr << "Failed to open shader file: " << fileName << '\n';
		exit(1);
	}
	const GLint size = (GLint) file.tellg();
	char* src = new char[size]; // delete[] ???
	file.seekg(0);
	file.read(src, size);
	file.close();
	glShaderSource(shader, 1, &src, &size);
	glCompileShader(shader);
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

static const char* getTypeName(GLuint type) {
	switch(type) {
	case GL_FLOAT_VEC2:
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
	const filesystem::path shaderDir = argv[2];
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
		getRessources<GL_UNIFORM, GL_LOCATION, GL_TYPE>(prg, prog.uniforms, [](Uniform &u, GLint* vals) {
			u.index = vals[1];
			u.type = vals[2];
		});
		getRessources<GL_SHADER_STORAGE_BLOCK, GL_BUFFER_BINDING>(prg, prog.buffers, [](Buffer &b, GLint* vals) {
			b.binding = vals[1];
		});
		glDeleteProgram(prg);
	}
	for(const GLuint &shader : vertShaders | views::elements<1>) glDeleteShader(shader);
	for(const GLuint &shader : fragShaders | views::elements<1>) glDeleteShader(shader);

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
	Hfile << "\n\t\ttemplate<GLuint attribIndex, GLint size, GLenum type>\n";
	Hfile << "\t\tvoid __bind(GLuint VAO, GLuint bindingIndex, GLuint offset) const {\n";
	Hfile << "\t\t\tglEnableVertexArrayAttrib(VAO, attribIndex);\n";
	Hfile << "\t\t\tglVertexArrayAttribBinding(VAO, attribIndex, bindingIndex);\n";
	Hfile << "\t\t\tglVertexArrayAttribFormat(VAO, attribIndex, size, type, GL_FALSE, offset);\n";
	Hfile << "\t\t}\n";
	Hfile << "\t};\n";
	for(const Prog &prog : progs) {
		Hfile << "\n\tstruct : Program {\n";
		for(const Attribute &a : prog.attributes) {
			Hfile << "\t\tinline void bind_" << a.name << "(GLuint VAO, GLuint bindingIndex, GLuint offset) const {\n";
			Hfile << "\t\t\t__bind<" << a.index << ", " << getTypeSize(a.type) << ", " << getTypeName(a.type) << ">(VAO, bindingIndex, offset);\n";
			Hfile << "\t\t}\n";
		}
		for(const Buffer &b : prog.buffers) {
			Hfile << "\t\tinline void bind_" << b.name << "(GLuint SSBO) const {\n";
			Hfile << "\t\t\tglBindBufferBase(GL_SHADER_STORAGE_BUFFER, " << b.binding << ", SSBO);\n";
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
	Cfile << "\tchar* src = new char[size];\n";
	Cfile << "\tfile.seekg(0);\n";
	Cfile << "\tfile.read(src, size);\n";
	Cfile << "\tfile.close();\n";
	Cfile << "\tglShaderSource(shader, 1, &src, &size);\n";
	Cfile << "\tdelete[] src;\n";
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
	Cfile.close();

	return 0;
}