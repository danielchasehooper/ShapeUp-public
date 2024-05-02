out vec4 finalColor;
// int vec2 texCoord;
uniform vec3 viewEye; 
uniform vec3 viewCenter; 
uniform vec2 resolution;

vec4 castRay( in vec3 ro, in vec3 rd )
{
    float tmin = 0.1;
    float tmax = 300.0;

    float t = tmin;
    vec3 m = vec3(0);
    for( int i=0; i<64; i++ )
    {
        float precis = 0.0001*t;
        vec4 res = signed_distance_field( ro+rd*t );
        if( res.x<precis || t>tmax ) break;
        t += res.x;
        m = res.gba;
    }

    if( t>tmax ) m=vec3(0);
    return vec4( t, m );
}

mat3 setCamera( in vec3 ro, in vec3 ta, float cr )
{
    vec3 cw = normalize(ta-ro);
    vec3 cp = vec3(sin(cr), cos(cr),0.0);
    vec3 cu = normalize( cross(cw,cp) );
    vec3 cv = normalize( cross(cu,cw) );
    return mat3( cu, cv, cw );
}

void main()
{
    vec2 p = (-resolution.xy + 2.0*gl_FragCoord.xy)/resolution.y;

    mat3 camera_to_world = setCamera( viewEye, viewCenter, 0.0 );
    vec3 ray_direction = camera_to_world * normalize( vec3(p.xy,2.0) );

    finalColor = vec4(castRay( viewEye, ray_direction ).gba, 1);
}
 
