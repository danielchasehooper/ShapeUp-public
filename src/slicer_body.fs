in vec2 fragTexCoord;
out vec4 finalColor;
uniform float z;

void main()
{
    float sdf_value = signed_distance_field(vec3(fragTexCoord, z)).x;
    finalColor =  vec4(vec3(sdf_value), 1.);
}
