#version 330
out vec4 outputColor;
smooth in vec2 x;
flat in vec2 dim;
uniform float scale;
uniform int fill;
uniform vec3 color;

void main() {
	float border = 3;
	float alpha = .2;
	if(fill == 0) {
		alpha = 0;
	}
	if((x.x < border) || (x.x > dim.x-border)) {
		alpha = 1;
	}
	if((x.y < border) || (x.y > dim.y-border)) {
		alpha = 1;
	}
	outputColor = vec4(color,alpha); //fixme
}
