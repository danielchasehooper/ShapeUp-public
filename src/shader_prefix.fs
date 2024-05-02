float sdRoundBox( vec3 p, vec3 b, float r )
{
  vec3 q = abs(p) - b;
  return length(max(q,0.0)) + min(max(q.x,max(q.y,q.z)),0.0) - r;
}

float RoundBox( vec3 p, vec3 b, float r )
{
  vec3 q = abs(p) - b;
  return length(max(q,0.0)) + min(max(q.x,max(q.y,q.z)),0.0) - r;
}


vec4 opSmoothUnion( vec4 a, vec4 b, float blend )
{
    float h =  max( blend-abs(a.x-b.x), 0.0 )/blend;
    float m = h*h*0.5;
    float s = m*blend*(1.0/2.0);
    return (a.x<b.x) ? vec4(a.x-s,mix(a.gba,b.gba,m)) : vec4(b.x-s,mix(a.gba,b.gba,1.0-m));
}

vec4 BlobbyMin( vec4 a, vec4 b, float blend )
{
    float h =  max( blend-abs(a.x-b.x), 0.0 )/blend;
    float m = h*h*0.5;
    float s = m*blend*(1.0/2.0);
    return (a.x<b.x) ? vec4(a.x-s,mix(a.gba,b.gba,m)) : vec4(b.x-s,mix(a.gba,b.gba,1.0-m));
}

vec4 Min( vec4 a, vec4 b )
{
    return (a.x<b.x) ? a : b;
}

vec4 opSmoothUnionSteppedColor( vec4 a, vec4 b, float blend )
{
    float h =  max( blend-abs(a.x-b.x), 0.0 )/blend;
    float m = h*h*0.5;
    float s = m*blend*(1.0/2.0);
    return (a.x<b.x) ? vec4(a.x-s,a.gba) : vec4(b.x-s,b.gba);
}

vec4 opSmoothSubtraction( vec4 d1, vec4 d2, float k ) {
    float dist = opSmoothUnion(d1,vec4(-d2.x, d2.gba),k).x;
    return vec4(-dist, d2.gba); 
}

vec4 opS( vec4 d1, vec4 d2 )
{
    return vec4(max(-d2.x,d1.x), d1.gba);
}

vec4 opU( vec4 d1, vec4 d2 )
{
    return (d1.x<d2.x) ? d1 : d2;
}

vec3 opSymX( vec3 p )
{
    p.x = abs(p.x);
    return p;
}
vec3 opSymY( vec3 p )
{
    p.y = abs(p.y);
    return p;
}
vec3 opSymZ( vec3 p )
{
    p.z = abs(p.z);
    return p;
}
vec3 opSymXY( vec3 p )
{
    p.xy = abs(p.xy);
    return p;
}
vec3 opSymXZ( vec3 p )
{
    p.xz = abs(p.xz);
    return p;
}
vec3 opSymYZ( vec3 p )
{
    p.yz = abs(p.yz);
    return p;
}
vec3 opSymXYZ( vec3 p )
{
    p.xyz = abs(p.xyz);
    return p;
}

vec3 opRotateXYZ( vec3 p, vec3 theta)
{
    float cz = cos(theta.z);
    float sz = sin(theta.z);
    float cy = cos(theta.y);
    float sy = sin(theta.y);
    float cx = cos(theta.x);
    float sx = sin(theta.x);

    mat3 mat = mat3(
                cz*cy,
                cz*sy*sx - cx*sz,
                sz*sx + cz*cx*sy,

                cy*sz,
                cz*cx + sz*sy*sx,
                cx*sz*sy - cz*sx,

                -sy,
                cy*sx,
                cy*cx);

    return mat*p;
}


