#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <memory>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/string_cast.hpp>

// OpenGL library includes
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <debuggl.h>
#include "menger.h"
#include "camera.h"

int window_width = 800, window_height = 600;

// VBO and VAO descriptors.
enum { kVertexBuffer, kIndexBuffer, kNumVbos };

// These are our VAOs.
enum { kGeometryVao, kFloorVao, kOceanVao, kNumVaos };

GLuint g_array_objects[kNumVaos];  // This will store the VAO descriptors.
GLuint g_buffer_objects[kNumVaos][kNumVbos];  // These will store VBO descriptors.

// C++ 11 String Literal
// See http://en.cppreference.com/w/cpp/language/string_literal
const char* vertex_shader =
R"zzz(#version 400 core
in vec4 vertex_position;
uniform mat4 view;
uniform vec4 light_position;
out vec4 vs_light_direction;
void main()
{
	gl_Position = view * vertex_position;
	vs_light_direction = -gl_Position + view * light_position;
}
)zzz";

const char* geometry_shader =
R"zzz(#version 400 core
layout (triangles) in;
layout (triangle_strip, max_vertices = 3) out;
uniform mat4 projection;
uniform mat4 view;
in vec4 vs_light_direction[];
flat out vec4 normal;
out vec4 light_direction;
out vec4 world_position;
out vec3 bary;
void main()
{
	int n = 0;
	// size = gl_in[1].gl_Position.xyz - gl_in[0].gl_Position.xyz;
	normal = normalize(vec4(cross(gl_in[1].gl_Position.xyz - gl_in[0].gl_Position.xyz, gl_in[2].gl_Position.xyz - gl_in[0].gl_Position.xyz), 0.0f));
	for (n = 0; n < gl_in.length(); n++) {
		vec3 temp = vec3(0.0f, 0.0f, 0.0f);
		temp[n] = 1.0f;
		bary = temp;
		light_direction = vs_light_direction[n];
		gl_Position = projection * gl_in[n].gl_Position;
		world_position = inverse(view) * gl_in[n].gl_Position;
		EmitVertex();
	}
	EndPrimitive();
}
)zzz";

const char* fragment_shader =
R"zzz(#version 400 core
flat in vec4 normal;
uniform mat4 view;
in vec4 light_direction;
out vec4 fragment_color;
void main()
{
	vec4 world_normal = inverse(view) * normal;
	vec4 color = vec4(abs(world_normal.xyz), 1.0);
	float dot_nl = dot(normalize(light_direction), normalize(normal));
	dot_nl = clamp(dot_nl, 0.0, 1.0);
	fragment_color = clamp(dot_nl * color, 0.0, 1.0);
}
)zzz";

// FIXME: Implement shader effects with an alternative shader.
const char* floor_fragment_shader =
R"zzz(#version 400 core
flat in vec4 normal;
in vec4 light_direction;
in vec4 world_position;
in vec3 bary;
uniform bool wireframe;
uniform vec4 light_position;
out vec4 fragment_color;
void main()
{
	if(wireframe && min(min(bary[0], bary[1]),bary[2]) < .0025f) {
		fragment_color = vec4(0, 1.0f, 0, 1.0f);
	} else {
		float x = world_position.x;
		float y = world_position.z;
		float f = mod(floor(x) + floor(y), 2);
		vec4 color = vec4(f, f, f, 1.0);
		float dot_nl = dot(normalize(light_direction), normalize(normal));
		dot_nl = clamp(dot_nl, 0.0, 1.0);
		fragment_color = clamp(dot_nl * color, 0.0, 1.0);
	}
}
)zzz";

const char* floor_tesscontrol_shader =
R"zzz(#version 400 core
layout (vertices = 3) out;
in vec4 vs_light_direction[];
uniform float tess_level_inner;
uniform float tess_level_outer;
out vec4 tcs_light_direction[];
void main()
{
	gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;
	tcs_light_direction[gl_InvocationID] = vs_light_direction[gl_InvocationID];
	if(gl_InvocationID == 0){
		gl_TessLevelInner[0] = tess_level_inner;
		gl_TessLevelOuter[0] = tess_level_outer;
		gl_TessLevelOuter[1] = tess_level_outer;
		gl_TessLevelOuter[2] = tess_level_outer;
	}
}
)zzz";

const char* floor_tesseval_shader =
R"zzz(#version 400 core
layout(triangles) in;
in vec4 tcs_light_direction[];
out vec4 vs_light_direction;
void main()
{
	// in triangles, gl_TessCoord is bary
	gl_Position = (gl_TessCoord.x * gl_in[0].gl_Position) + (gl_TessCoord.y * gl_in[1].gl_Position) + (gl_TessCoord.z * gl_in[2].gl_Position);
	vs_light_direction = (gl_TessCoord.x * tcs_light_direction[0]) + (gl_TessCoord.y * tcs_light_direction[1]) + (gl_TessCoord.z * tcs_light_direction[2]);
}
)zzz";

const char* ocean_tesscontrol_shader =
R"zzz(#version 400 core
layout (vertices = 4) out;
in vec4 vs_light_direction[];
uniform float tess_level_inner;
uniform float tess_level_outer;
out vec4 tcs_light_direction[];
void main()
{
	gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;
	tcs_light_direction[gl_InvocationID] = vs_light_direction[gl_InvocationID];
	if(gl_InvocationID == 0){
		gl_TessLevelInner[0] = tess_level_inner;
		gl_TessLevelInner[1] = tess_level_inner;
		gl_TessLevelOuter[0] = tess_level_outer;
		gl_TessLevelOuter[1] = tess_level_outer;
		gl_TessLevelOuter[2] = tess_level_outer;
		gl_TessLevelOuter[3] = tess_level_outer;
	}
}
)zzz";

const char* ocean_tesseval_shader =
R"zzz(#version 400 core
layout(quads) in;
in vec4 tcs_light_direction[];
out vec4 vs_light_direction;
void main()
{
	// in triangles, gl_TessCoord is u,v
	vec4 first = mix(gl_in[0].gl_Position, gl_in[1].gl_Position, gl_TessCoord.x);
	vec4 second = mix(gl_in[3].gl_Position, gl_in[2].gl_Position, gl_TessCoord.x);
	gl_Position = mix(first, second, gl_TessCoord.y);
}
)zzz";

const char* ocean_fragment_shader =
R"zzz(#version 400 core
flat in vec4 normal;
in vec4 light_direction;
in vec4 world_position;
in vec3 bary;
uniform bool wireframe;
uniform vec4 light_position;
out vec4 fragment_color;
void main()
{
	if(wireframe && min(min(bary[0], bary[1]),bary[2]) < .0025f) {
		fragment_color = vec4(0, 1.0f, 0, 1.0f);
	} else {
		fragment_color = vec4(0.0f, 0.0f, 1.0f, 1.0f);
	}
}
)zzz";

void
CreateFloor(std::vector<glm::vec4>& vertices,
        std::vector<glm::uvec3>& indices)
{
	vertices.push_back(glm::vec4(-10.0f, -3.0f, -10.0f, 1.0f));
	vertices.push_back(glm::vec4(-10.0f, -3.0f, 10.0f, 1.0f));
	vertices.push_back(glm::vec4(10.0f, -3.0f, -10.0f, 1.0f));
	vertices.push_back(glm::vec4(10.0f, -3.0f, 10.0f, 1.0f));
	indices.push_back(glm::uvec3(0, 1, 3));
	indices.push_back(glm::uvec3(0, 3, 2));
}

void
CreateOcean(std::vector<glm::vec4>& vertices,
        std::vector<glm::uvec4>& indices)
{
	//16 x 16 patches
	for(int i = 0; i < 16; i++) {
		for(int k = 0; k < 16; k++) {
			uint32_t offset = vertices.size();
			vertices.push_back(glm::vec4(-20.0f + 2.5f * i, -2.0f, -20.0f + 2.5f * k, 1.0f));
			vertices.push_back(glm::vec4(-20.0f + 2.5f * (i + 1), -2.0f, -20.0f + 2.5f * k, 1.0f));
			vertices.push_back(glm::vec4(-20.0f + 2.5f * i, -2.0f, -20.0f + 2.5f * (k + 1), 1.0f));
			vertices.push_back(glm::vec4(-20.0f + 2.5f * (i + 1), -2.0f, -20.0f + 2.5f * (k + 1), 1.0f));
			indices.push_back(glm::uvec4(offset, offset + 1, offset + 3, offset + 2));
		}
	}
}

void
CreateTriangle(std::vector<glm::vec4>& vertices,
        std::vector<glm::uvec3>& indices)
{
	vertices.push_back(glm::vec4(-0.5f, -0.5f, -0.5f, 1.0f));
	vertices.push_back(glm::vec4(0.5f, -0.5f, -0.5f, 1.0f));
	vertices.push_back(glm::vec4(0.0f, 0.5f, -0.5f, 1.0f));
	indices.push_back(glm::uvec3(0, 1, 2));
}

// FIXME: Save geometry to OBJ file
void
SaveObj(const std::string& file,
        const std::vector<glm::vec4>& vertices,
        const std::vector<glm::uvec3>& indices)
{
	std::ofstream f;
	f.open(file);
	for (auto &v : vertices) {
    	f << "v " << v.x << " " << v.y << " " << v.z << std::endl;
	}
	for (auto &i : indices) {
    	f << "f " << (i.x + 1) << " " << (i.y + 1) << " " << (i.z + 1) << std::endl;
	}
	f.close();
}

void
ErrorCallback(int error, const char* description)
{
	std::cerr << "GLFW Error: " << description << "\n";
}


std::shared_ptr<Menger> g_menger;
Camera g_camera;
bool save_obj = false;
bool wireframe = true;
bool toggleFaces = true;
float tess_level_inner = 3.0f;
float tess_level_outer = 3.0f;


void
KeyCallback(GLFWwindow* window,
            int key,
            int scancode,
            int action,
            int mods)
{
	// Note:
	// This is only a list of functions to implement.
	// you may want to re-organize this piece of code.
	if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
		glfwSetWindowShouldClose(window, GL_TRUE);
	else if (key == GLFW_KEY_S && mods == GLFW_MOD_CONTROL && action == GLFW_RELEASE) {
		// FIXME: save geometry to OBJ
		save_obj = true;
	} else if (key == GLFW_KEY_W && action != GLFW_RELEASE) {
		g_camera.strafe_forward(1);
	} else if (key == GLFW_KEY_S && action != GLFW_RELEASE) {
		g_camera.strafe_forward(-1);
	} else if (key == GLFW_KEY_A && action != GLFW_RELEASE) {
		g_camera.strafe_tangent(-1);
	} else if (key == GLFW_KEY_D && action != GLFW_RELEASE) {
		g_camera.strafe_tangent(1);
	} else if (key == GLFW_KEY_F && mods == GLFW_MOD_CONTROL && action == GLFW_RELEASE) {
		toggleFaces = !toggleFaces;
	} else if (key == GLFW_KEY_F && action == GLFW_RELEASE) {
		wireframe = !wireframe;
	} else if (key == GLFW_KEY_LEFT && action != GLFW_RELEASE) {
		g_camera.roll(-1);
	} else if (key == GLFW_KEY_RIGHT && action != GLFW_RELEASE) {
		g_camera.roll(1);
	} else if (key == GLFW_KEY_DOWN && action != GLFW_RELEASE) {
		g_camera.strafe_up(-1);
	} else if (key == GLFW_KEY_UP && action != GLFW_RELEASE) {
		g_camera.strafe_up(1);
	} else if (key == GLFW_KEY_C && action == GLFW_RELEASE) {
		// FIXME: FPS mode on/off
		g_camera.fps = !g_camera.fps;
	} else if (key == GLFW_KEY_MINUS && action != GLFW_RELEASE){
		if (tess_level_outer > 1.0f){
			tess_level_outer -= 1.0f;
		}
	} else if (key == GLFW_KEY_EQUAL && action != GLFW_RELEASE){
		if (tess_level_outer < 50.0f){
			tess_level_outer += 1.0f;
		}
	}  else if (key == GLFW_KEY_COMMA && action != GLFW_RELEASE){
		if (tess_level_inner > 1.0f){
			tess_level_inner -= 1.0f;
		}
	} else if (key == GLFW_KEY_PERIOD && action != GLFW_RELEASE){
		if (tess_level_inner < 50.0f){
			tess_level_inner += 1.0f;
		}
	}
	if (!g_menger)
		return ; // 0-4 only available in Menger mode.
	if (key == GLFW_KEY_0 && action != GLFW_RELEASE) {
		g_menger->set_nesting_level(0);
		// FIXME: Change nesting level of g_menger
		// Note: GLFW_KEY_0 - 4 may not be continuous.
	} else if (key == GLFW_KEY_1 && action != GLFW_RELEASE) {
		g_menger->set_nesting_level(1);
	} else if (key == GLFW_KEY_2 && action != GLFW_RELEASE) {
		g_menger->set_nesting_level(2);
	} else if (key == GLFW_KEY_3 && action != GLFW_RELEASE) {
		g_menger->set_nesting_level(3);
	} else if (key == GLFW_KEY_4 && action != GLFW_RELEASE) {
		g_menger->set_nesting_level(4);
	}
}

int g_current_button;
bool g_mouse_pressed;

void
MousePosCallback(GLFWwindow* window, double mouse_x, double mouse_y)
{
	if (!g_mouse_pressed)
		return;
	if (g_current_button == GLFW_MOUSE_BUTTON_LEFT) {
		g_camera.rotate(g_camera.last_x - mouse_x, g_camera.last_y - mouse_y);
	} else if (g_current_button == GLFW_MOUSE_BUTTON_RIGHT) {
		if(g_camera.last_y > mouse_y) {
			g_camera.zoom(1);
		} else if(g_camera.last_y < mouse_y) {
			g_camera.zoom(-1);
		}
	} else if (g_current_button == GLFW_MOUSE_BUTTON_MIDDLE) {
		if(g_camera.last_y > mouse_y) {
			g_camera.strafe_up(1);
		} else if(g_camera.last_y < mouse_y) {
			g_camera.strafe_up(-1);
		}
		if(g_camera.last_x > mouse_x) {
			g_camera.strafe_tangent(-1);
		} else if(g_camera.last_x < mouse_x) {
			g_camera.strafe_tangent(1);
		}
	}
	g_camera.last_y = mouse_y;
	g_camera.last_x = mouse_x;
}

void
MouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
{
	g_mouse_pressed = (action == GLFW_PRESS);
	g_current_button = button;
}

int main(int argc, char* argv[])
{
	std::string window_title = "Menger";
	if (!glfwInit()) exit(EXIT_FAILURE);
	g_menger = std::make_shared<Menger>();
	glfwSetErrorCallback(ErrorCallback);

	// Ask an OpenGL 4.1 core profile context
	// It is required on OSX and non-NVIDIA Linux
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	GLFWwindow* window = glfwCreateWindow(window_width, window_height,
			&window_title[0], nullptr, nullptr);
	CHECK_SUCCESS(window != nullptr);
	glfwMakeContextCurrent(window);
	glewExperimental = GL_TRUE;

	CHECK_SUCCESS(glewInit() == GLEW_OK);
	glGetError();  // clear GLEW's error for it
	glfwSetKeyCallback(window, KeyCallback);
	glfwSetCursorPosCallback(window, MousePosCallback);
	glfwSetMouseButtonCallback(window, MouseButtonCallback);
	glfwSwapInterval(1);
	const GLubyte* renderer = glGetString(GL_RENDERER);  // get renderer string
	const GLubyte* version = glGetString(GL_VERSION);    // version as a string
	std::cout << "Renderer: " << renderer << "\n";
	std::cout << "OpenGL version supported:" << version << "\n";

	std::vector<glm::vec4> obj_vertices;
	std::vector<glm::uvec3> obj_faces;

	std::vector<glm::vec4> floor_vertices;
	std::vector<glm::uvec3> floor_faces;
	CreateFloor(floor_vertices, floor_faces);

	std::vector<glm::vec4> ocean_vertices;
	std::vector<glm::uvec4> ocean_faces;
	CreateOcean(ocean_vertices, ocean_faces);

	g_menger->set_nesting_level(1);

	glm::vec4 min_bounds = glm::vec4(std::numeric_limits<float>::max());
	glm::vec4 max_bounds = glm::vec4(-std::numeric_limits<float>::max());
	for (const auto& vert : obj_vertices) {
		min_bounds = glm::min(vert, min_bounds);
		max_bounds = glm::max(vert, max_bounds);
	}
	std::cout << "min_bounds = " << glm::to_string(min_bounds) << "\n";
	std::cout << "max_bounds = " << glm::to_string(max_bounds) << "\n";

	// Setup our VAO array.
	CHECK_GL_ERROR(glGenVertexArrays(kNumVaos, &g_array_objects[0]));

	// Switch to the VAO for Geometry.
	CHECK_GL_ERROR(glBindVertexArray(g_array_objects[kGeometryVao]));

	// Generate buffer objects
	CHECK_GL_ERROR(glGenBuffers(kNumVbos, &g_buffer_objects[kGeometryVao][0]));

	// Setup vertex data in a VBO.
	CHECK_GL_ERROR(glBindBuffer(GL_ARRAY_BUFFER, g_buffer_objects[kGeometryVao][kVertexBuffer]));
	// NOTE: We do not send anything right now, we just describe it to OpenGL.
	CHECK_GL_ERROR(glBufferData(GL_ARRAY_BUFFER,
				sizeof(float) * obj_vertices.size() * 4, obj_vertices.data(),
				GL_STATIC_DRAW));
	CHECK_GL_ERROR(glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, 0));
	CHECK_GL_ERROR(glEnableVertexAttribArray(0));

	// Setup element array buffer.
	CHECK_GL_ERROR(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_buffer_objects[kGeometryVao][kIndexBuffer]));
	CHECK_GL_ERROR(glBufferData(GL_ELEMENT_ARRAY_BUFFER,
				sizeof(uint32_t) * obj_faces.size() * 3,
				obj_faces.data(), GL_STATIC_DRAW));


// Switch to the VAO for floor.
	CHECK_GL_ERROR(glBindVertexArray(g_array_objects[kFloorVao]));
	CHECK_GL_ERROR(glGenBuffers(kNumVbos, &g_buffer_objects[kFloorVao][0]));
	CHECK_GL_ERROR(glBindBuffer(GL_ARRAY_BUFFER, g_buffer_objects[kFloorVao][kVertexBuffer]));
	CHECK_GL_ERROR(glBufferData(GL_ARRAY_BUFFER,
				sizeof(float) * floor_vertices.size() * 4, floor_vertices.data(),
				GL_STATIC_DRAW));
	CHECK_GL_ERROR(glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, 0));
	CHECK_GL_ERROR(glEnableVertexAttribArray(0));
	CHECK_GL_ERROR(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_buffer_objects[kFloorVao][kIndexBuffer]));
	CHECK_GL_ERROR(glBufferData(GL_ELEMENT_ARRAY_BUFFER,
				sizeof(uint32_t) * floor_faces.size() * 3,
				floor_faces.data(), GL_STATIC_DRAW));


// Switch to the Ocean VAO
	CHECK_GL_ERROR(glBindVertexArray(g_array_objects[kOceanVao]));
	CHECK_GL_ERROR(glGenBuffers(kNumVbos, &g_buffer_objects[kOceanVao][0]));
	CHECK_GL_ERROR(glBindBuffer(GL_ARRAY_BUFFER, g_buffer_objects[kOceanVao][kVertexBuffer]));
	CHECK_GL_ERROR(glBufferData(GL_ARRAY_BUFFER,
				sizeof(float) * ocean_vertices.size() * 4, ocean_vertices.data(),
				GL_STATIC_DRAW));
	CHECK_GL_ERROR(glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, 0));
	CHECK_GL_ERROR(glEnableVertexAttribArray(0));
	CHECK_GL_ERROR(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_buffer_objects[kOceanVao][kIndexBuffer]));
	CHECK_GL_ERROR(glBufferData(GL_ELEMENT_ARRAY_BUFFER,
				sizeof(uint32_t) * ocean_faces.size() * 4,
				ocean_faces.data(), GL_STATIC_DRAW));


	// Setup vertex shader.
	GLuint vertex_shader_id = 0;
	const char* vertex_source_pointer = vertex_shader;
	CHECK_GL_ERROR(vertex_shader_id = glCreateShader(GL_VERTEX_SHADER));
	CHECK_GL_ERROR(glShaderSource(vertex_shader_id, 1, &vertex_source_pointer, nullptr));
	glCompileShader(vertex_shader_id);
	CHECK_GL_SHADER_ERROR(vertex_shader_id);

	// Setup geometry shader.
	GLuint geometry_shader_id = 0;
	const char* geometry_source_pointer = geometry_shader;
	CHECK_GL_ERROR(geometry_shader_id = glCreateShader(GL_GEOMETRY_SHADER));
	CHECK_GL_ERROR(glShaderSource(geometry_shader_id, 1, &geometry_source_pointer, nullptr));
	glCompileShader(geometry_shader_id);
	CHECK_GL_SHADER_ERROR(geometry_shader_id);

	// Setup fragment shader.
	GLuint fragment_shader_id = 0;
	const char* fragment_source_pointer = fragment_shader;
	CHECK_GL_ERROR(fragment_shader_id = glCreateShader(GL_FRAGMENT_SHADER));
	CHECK_GL_ERROR(glShaderSource(fragment_shader_id, 1, &fragment_source_pointer, nullptr));
	glCompileShader(fragment_shader_id);
	CHECK_GL_SHADER_ERROR(fragment_shader_id);

	// Setup fragment shader for the floor
	GLuint floor_fragment_shader_id = 0;
	const char* floor_fragment_source_pointer = floor_fragment_shader;
	CHECK_GL_ERROR(floor_fragment_shader_id = glCreateShader(GL_FRAGMENT_SHADER));
	CHECK_GL_ERROR(glShaderSource(floor_fragment_shader_id, 1,
				&floor_fragment_source_pointer, nullptr));
	glCompileShader(floor_fragment_shader_id);
	CHECK_GL_SHADER_ERROR(floor_fragment_shader_id);

	GLuint floor_tesscontrol_shader_id = 0;
	const char* floor_tesscontrol_source_pointer = floor_tesscontrol_shader;
	CHECK_GL_ERROR(floor_tesscontrol_shader_id = glCreateShader(GL_TESS_CONTROL_SHADER));
	CHECK_GL_ERROR(glShaderSource(floor_tesscontrol_shader_id, 1,
				&floor_tesscontrol_source_pointer, nullptr));
	glCompileShader(floor_tesscontrol_shader_id);
	CHECK_GL_SHADER_ERROR(floor_tesscontrol_shader_id);

	GLuint floor_tesseval_shader_id = 0;
	const char* floor_tesseval_source_pointer = floor_tesseval_shader;
	CHECK_GL_ERROR(floor_tesseval_shader_id = glCreateShader(GL_TESS_EVALUATION_SHADER));
	CHECK_GL_ERROR(glShaderSource(floor_tesseval_shader_id, 1,
				&floor_tesseval_source_pointer, nullptr));
	glCompileShader(floor_tesseval_shader_id);
	CHECK_GL_SHADER_ERROR(floor_tesseval_shader_id);

	GLuint ocean_tesscontrol_shader_id = 0;
	const char* ocean_tesscontrol_source_pointer = ocean_tesscontrol_shader;
	CHECK_GL_ERROR(ocean_tesscontrol_shader_id = glCreateShader(GL_TESS_CONTROL_SHADER));
	CHECK_GL_ERROR(glShaderSource(ocean_tesscontrol_shader_id, 1,
				&ocean_tesscontrol_source_pointer, nullptr));
	glCompileShader(ocean_tesscontrol_shader_id);
	CHECK_GL_SHADER_ERROR(ocean_tesscontrol_shader_id);

	GLuint ocean_tesseval_shader_id = 0;
	const char* ocean_tesseval_source_pointer = ocean_tesseval_shader;
	CHECK_GL_ERROR(ocean_tesseval_shader_id = glCreateShader(GL_TESS_EVALUATION_SHADER));
	CHECK_GL_ERROR(glShaderSource(ocean_tesseval_shader_id, 1,
				&ocean_tesseval_source_pointer, nullptr));
	glCompileShader(ocean_tesseval_shader_id);
	CHECK_GL_SHADER_ERROR(ocean_tesseval_shader_id);

	GLuint ocean_fragment_shader_id = 0;
	const char* ocean_fragment_source_pointer = ocean_fragment_shader;
	CHECK_GL_ERROR(ocean_fragment_shader_id = glCreateShader(GL_FRAGMENT_SHADER));
	CHECK_GL_ERROR(glShaderSource(ocean_fragment_shader_id, 1,
				&ocean_fragment_source_pointer, nullptr));
	glCompileShader(ocean_fragment_shader_id);
	CHECK_GL_SHADER_ERROR(ocean_fragment_shader_id);

	// Let's create our program.
	GLuint program_id = 0;
	CHECK_GL_ERROR(program_id = glCreateProgram());
	CHECK_GL_ERROR(glAttachShader(program_id, vertex_shader_id));
	CHECK_GL_ERROR(glAttachShader(program_id, fragment_shader_id));
	CHECK_GL_ERROR(glAttachShader(program_id, geometry_shader_id));

	// Bind attributes.
	CHECK_GL_ERROR(glBindAttribLocation(program_id, 0, "vertex_position"));
	CHECK_GL_ERROR(glBindFragDataLocation(program_id, 0, "fragment_color"));
	glLinkProgram(program_id);
	CHECK_GL_PROGRAM_ERROR(program_id);

	// Get the uniform locations.
	GLint projection_matrix_location = 0;
	CHECK_GL_ERROR(projection_matrix_location =
			glGetUniformLocation(program_id, "projection"));
	GLint view_matrix_location = 0;
	CHECK_GL_ERROR(view_matrix_location =
			glGetUniformLocation(program_id, "view"));
	GLint light_position_location = 0;
	CHECK_GL_ERROR(light_position_location =
			glGetUniformLocation(program_id, "light_position"));

	// FIXME: Setup another program for the floor, and get its locations.
	// Note: you can reuse the vertex and geometry shader objects
	GLuint floor_program_id = 0;
	CHECK_GL_ERROR(floor_program_id = glCreateProgram());
	CHECK_GL_ERROR(glAttachShader(floor_program_id, vertex_shader_id));
	CHECK_GL_ERROR(glAttachShader(floor_program_id, floor_fragment_shader_id));
	CHECK_GL_ERROR(glAttachShader(floor_program_id, floor_tesscontrol_shader_id));
	CHECK_GL_ERROR(glAttachShader(floor_program_id, floor_tesseval_shader_id));
	CHECK_GL_ERROR(glAttachShader(floor_program_id, geometry_shader_id));

	// Bind attributes.
	CHECK_GL_ERROR(glBindAttribLocation(floor_program_id, 0, "vertex_position"));
	CHECK_GL_ERROR(glBindFragDataLocation(floor_program_id, 0, "fragment_color"));
	glLinkProgram(floor_program_id);
	CHECK_GL_PROGRAM_ERROR(floor_program_id);

	// Get the uniform locations.
	GLint floor_projection_matrix_location = 0;
	CHECK_GL_ERROR(floor_projection_matrix_location =
			glGetUniformLocation(floor_program_id, "projection"));
	GLint floor_view_matrix_location = 0;
	CHECK_GL_ERROR(floor_view_matrix_location =
			glGetUniformLocation(floor_program_id, "view"));
	GLint floor_light_position_location = 0;
	CHECK_GL_ERROR(floor_light_position_location =
			glGetUniformLocation(floor_program_id, "light_position"));
	GLint floor_wireframe_location = 0;
	CHECK_GL_ERROR(floor_wireframe_location =
			glGetUniformLocation(floor_program_id, "wireframe"));
	GLint floor_tessinner_location = 0;
	CHECK_GL_ERROR(floor_tessinner_location =
			glGetUniformLocation(floor_program_id, "tess_level_inner"));
	GLint floor_tessouter_location = 0;
	CHECK_GL_ERROR(floor_tessouter_location =
			glGetUniformLocation(floor_program_id, "tess_level_outer"));

//ocean program
	GLuint ocean_program_id = 0;
	CHECK_GL_ERROR(ocean_program_id = glCreateProgram());
	CHECK_GL_ERROR(glAttachShader(ocean_program_id, vertex_shader_id));
	CHECK_GL_ERROR(glAttachShader(ocean_program_id, ocean_fragment_shader_id));
	CHECK_GL_ERROR(glAttachShader(ocean_program_id, ocean_tesscontrol_shader_id));
	CHECK_GL_ERROR(glAttachShader(ocean_program_id, ocean_tesseval_shader_id));
	CHECK_GL_ERROR(glAttachShader(ocean_program_id, geometry_shader_id));

	// Bind attributes.
	CHECK_GL_ERROR(glBindAttribLocation(ocean_program_id, 0, "vertex_position"));
	CHECK_GL_ERROR(glBindFragDataLocation(ocean_program_id, 0, "fragment_color"));
	glLinkProgram(ocean_program_id);
	CHECK_GL_PROGRAM_ERROR(ocean_program_id);

	// Get the uniform locations.
	GLint ocean_projection_matrix_location = 0;
	CHECK_GL_ERROR(ocean_projection_matrix_location =
			glGetUniformLocation(ocean_program_id, "projection"));
	GLint ocean_view_matrix_location = 0;
	CHECK_GL_ERROR(ocean_view_matrix_location =
			glGetUniformLocation(ocean_program_id, "view"));
	GLint ocean_light_position_location = 0;
	CHECK_GL_ERROR(ocean_light_position_location =
			glGetUniformLocation(ocean_program_id, "light_position"));
	GLint ocean_wireframe_location = 0;
	CHECK_GL_ERROR(ocean_wireframe_location =
			glGetUniformLocation(ocean_program_id, "wireframe"));
	GLint ocean_tessinner_location = 0;
	CHECK_GL_ERROR(ocean_tessinner_location =
			glGetUniformLocation(ocean_program_id, "tess_level_inner"));
	GLint ocean_tessouter_location = 0;
	CHECK_GL_ERROR(ocean_tessouter_location =
			glGetUniformLocation(ocean_program_id, "tess_level_outer"));


	glm::vec4 light_position = glm::vec4(-10.0f, 10.0f, 0.0f, 1.0f);
	float aspect = 0.0f;
	float theta = 0.0f;
	while (!glfwWindowShouldClose(window)) {
		// Setup some basic window stuff.
		glfwGetFramebufferSize(window, &window_width, &window_height);
		glViewport(0, 0, window_width, window_height);
		glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
		glEnable(GL_DEPTH_TEST);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glDepthFunc(GL_LESS);

		if(save_obj){
			SaveObj("geometry.obj", obj_vertices, obj_faces);
			save_obj = false;
		}

		// Switch to the Geometry VAO.
		CHECK_GL_ERROR(glBindVertexArray(g_array_objects[kGeometryVao]));
		CHECK_GL_ERROR(glBindBuffer(GL_ARRAY_BUFFER, g_buffer_objects[kGeometryVao][kVertexBuffer]));
		CHECK_GL_ERROR(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_buffer_objects[kGeometryVao][kIndexBuffer]));

		if (g_menger && g_menger->is_dirty()) {
			obj_vertices.clear();
			obj_faces.clear();
			g_menger->generate_geometry(obj_vertices, obj_faces);
			g_menger->set_clean();

			CHECK_GL_ERROR(glBufferData(GL_ARRAY_BUFFER,
				sizeof(float) * obj_vertices.size() * 4, obj_vertices.data(),
				GL_STATIC_DRAW));

			CHECK_GL_ERROR(glBufferData(GL_ELEMENT_ARRAY_BUFFER,
				sizeof(uint32_t) * obj_faces.size() * 3,
				obj_faces.data(), GL_STATIC_DRAW));

			// FIXME: Upload your vertex data here.

		}

		// Compute the projection matrix.
		aspect = static_cast<float>(window_width) / window_height;
		glm::mat4 projection_matrix =
			glm::perspective(glm::radians(45.0f), aspect, 0.0001f, 1000.0f);

		// Compute the view matrix
		// FIXME: change eye and center through mouse/keyboard events.
		glm::mat4 view_matrix = g_camera.get_view_matrix();

		// Use our program.
		CHECK_GL_ERROR(glUseProgram(program_id));


		// Pass uniforms in.
		CHECK_GL_ERROR(glUniformMatrix4fv(projection_matrix_location, 1, GL_FALSE,
					&projection_matrix[0][0]));
		CHECK_GL_ERROR(glUniformMatrix4fv(view_matrix_location, 1, GL_FALSE,
					&view_matrix[0][0]));
		CHECK_GL_ERROR(glUniform4fv(light_position_location, 1, &light_position[0]));

		CHECK_GL_ERROR(glPolygonMode(GL_FRONT_AND_BACK, GL_FILL));	
		// Draw our triangles.
		CHECK_GL_ERROR(glDrawElements(GL_TRIANGLES, obj_faces.size() * 3, GL_UNSIGNED_INT, 0));


		// FIXME: Render the floor
		// Note: What you need to do is
		// 	1. Switch VAO
		// 	2. Switch Program
		// 	3. Pass Uniforms
		// 	4. Call glDrawElements, since input geometry is
		// 	indicated by VAO.
		CHECK_GL_ERROR(glUseProgram(floor_program_id));
		CHECK_GL_ERROR(glBindVertexArray(g_array_objects[kFloorVao]));
		CHECK_GL_ERROR(glBindBuffer(GL_ARRAY_BUFFER, g_buffer_objects[kFloorVao][kVertexBuffer]));
		CHECK_GL_ERROR(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_buffer_objects[kFloorVao][kIndexBuffer]));

		// Pass uniforms in.
		CHECK_GL_ERROR(glUniformMatrix4fv(floor_projection_matrix_location, 1, GL_FALSE,
					&projection_matrix[0][0]));
		CHECK_GL_ERROR(glUniformMatrix4fv(floor_view_matrix_location, 1, GL_FALSE,
					&view_matrix[0][0]));
		CHECK_GL_ERROR(glUniform4fv(floor_light_position_location, 1, &light_position[0]));
		CHECK_GL_ERROR(glUniform1i(floor_wireframe_location, wireframe));
		CHECK_GL_ERROR(glUniform1f(floor_tessouter_location, tess_level_outer));
		CHECK_GL_ERROR(glUniform1f(floor_tessinner_location, tess_level_inner));

		if(toggleFaces) {
			CHECK_GL_ERROR(glPolygonMode(GL_FRONT_AND_BACK, GL_FILL));
		} else {
			CHECK_GL_ERROR(glPolygonMode(GL_FRONT_AND_BACK, GL_LINE));
		}
		// Draw our triangles.
		CHECK_GL_ERROR(glPatchParameteri(GL_PATCH_VERTICES, 3));
		CHECK_GL_ERROR(glDrawElements(GL_PATCHES, floor_faces.size() * 3, GL_UNSIGNED_INT, 0));

		//ocean drawing
		CHECK_GL_ERROR(glUseProgram(ocean_program_id));
		CHECK_GL_ERROR(glBindVertexArray(g_array_objects[kOceanVao]));
		CHECK_GL_ERROR(glBindBuffer(GL_ARRAY_BUFFER, g_buffer_objects[kOceanVao][kVertexBuffer]));
		CHECK_GL_ERROR(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_buffer_objects[kOceanVao][kIndexBuffer]));

		// Pass uniforms in.
		CHECK_GL_ERROR(glUniformMatrix4fv(ocean_projection_matrix_location, 1, GL_FALSE,
					&projection_matrix[0][0]));
		CHECK_GL_ERROR(glUniformMatrix4fv(ocean_view_matrix_location, 1, GL_FALSE,
					&view_matrix[0][0]));
		CHECK_GL_ERROR(glUniform4fv(ocean_light_position_location, 1, &light_position[0]));
		CHECK_GL_ERROR(glUniform1i(ocean_wireframe_location, wireframe));
		CHECK_GL_ERROR(glUniform1f(ocean_tessouter_location, tess_level_outer));
		CHECK_GL_ERROR(glUniform1f(ocean_tessinner_location, tess_level_inner));

		if(toggleFaces) {
			CHECK_GL_ERROR(glPolygonMode(GL_FRONT_AND_BACK, GL_FILL));
		} else {
			CHECK_GL_ERROR(glPolygonMode(GL_FRONT_AND_BACK, GL_LINE));
		}
		// Draw our triangles.
		CHECK_GL_ERROR(glPatchParameteri(GL_PATCH_VERTICES, 4));
		CHECK_GL_ERROR(glDrawElements(GL_PATCHES, ocean_faces.size() * 4, GL_UNSIGNED_INT, 0));


		// Poll and swap.
		glfwPollEvents();

		glfwSwapBuffers(window);
	}
	glfwDestroyWindow(window);
	glfwTerminate();
	exit(EXIT_SUCCESS);
}
