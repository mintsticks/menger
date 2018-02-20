#ifndef CAMERA_H
#define CAMERA_H

#include <glm/glm.hpp>

class Camera {
public:
	glm::mat4 get_view_matrix() const;
	// FIXME: add functions to manipulate camera objects.
	void strafe_tangent(int direction);
	void strafe_up(int direction);
	void strafe_forward(int direction);
	void zoom(int direction);
	void roll(int direction);
	void rotate(float dx, float dy);
	float last_y = 0.0f;
	float last_x = 0.0f;
	bool fps = true;
private:
	float camera_distance_ = 3.0f;
	glm::vec3 look_ = glm::vec3(0.0f, 0.0f, -1.0f);
	glm::vec3 up_ = glm::vec3(0.0f, 1.0, 0.0f);
	glm::vec3 eye_ = glm::vec3(0.0f, 0.0f, camera_distance_);
	// Note: you may need additional member variables
};

#endif
