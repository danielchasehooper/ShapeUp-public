vec4 castRay( in vec3 ro, in vec3 rd )
{
    float tmin = 0.1;
    float tmax = 300.0;

    float t = tmin;
    vec3 m = vec3(-1);
    for( int i=0; i<64; i++ )
    {
        float precis = 0.0001*t;
        vec4 res = signed_distance_field( ro+rd*t );
        if( res.x<precis || t>tmax ) break;
        t += res.x;
        m = res.gba;
    }

    if( t>tmax ) m=vec3(-1);
    return vec4( t, m );
}


float calcSoftshadow( in vec3 ro, in vec3 rd, in float mint, in float tmax )
{
    float res = 1.0;
    float t = mint;
    for( int i=0; i<16; i++ )
    {
        float h = signed_distance_field( ro + rd*t ).x;
        res = min( res, 8.0*h/t );
        t += clamp( h, 0.02, 0.10 );
        if( h<0.001 || t>tmax ) break;
    }
    return clamp( res, 0.0, 1.0 );
}

vec3 calcNormal( in vec3 pos )
{
    vec2 e = vec2(1.0,-1.0)*0.5773*0.0005;
    return normalize( e.xyy*signed_distance_field( pos + e.xyy ).x +
                      e.yyx*signed_distance_field( pos + e.yyx ).x +
                      e.yxy*signed_distance_field( pos + e.yxy ).x +
                      e.xxx*signed_distance_field( pos + e.xxx ).x );
    /*
    vec3 eps = vec3( 0.0005, 0.0, 0.0 );
    vec3 nor = vec3(
        signed_distance_field(pos+eps.xyy).x - signed_distance_field(pos-eps.xyy).x,
        signed_distance_field(pos+eps.yxy).x - signed_distance_field(pos-eps.yxy).x,
        signed_distance_field(pos+eps.yyx).x - signed_distance_field(pos-eps.yyx).x );
    return normalize(nor);
    */
}

float calcAO( in vec3 pos, in vec3 nor )
{
    float occ = 0.0;
    float sca = 1.0;
    for( int i=0; i<5; i++ )
    {
        float hr = 0.01 + 0.12*float(i)/4.0;
        vec3 aopos =  nor * hr + pos;
        float dd = signed_distance_field( aopos ).x;
        occ += -(dd-hr)*sca;
        sca *= 0.95;
    }
    return clamp( 1.0 - 3.0*occ, 0.0, 1.0 );
}

vec3 render( in vec3 ro, in vec3 rd )
{
    vec3 color =
#ifdef FALSE_COLOR_MODE
    vec3(0.);
#else
    vec3(0.4, 0.5, 0.6) +rd.y*0.4;
#endif
    vec4 result = castRay(ro,rd);
    float t = result.x;
    vec3 m = result.yzw;
    if( m.r>-0.5 )
    {
        vec3 pos = ro + t*rd;
        vec3 nor = calcNormal( pos );
        // vec3 ref = reflect( rd, nor );

        // material
        color = m;

        #ifndef FALSE_COLOR_MODE

        // lighting
        // float occ = calcAO( pos, nor );
        vec3  light_dir = normalize( vec3(cos(-0.4), sin(0.7), -0.6) );
        vec3  hal = normalize( light_dir-rd );
        float ambient = clamp( 0.5+0.5*nor.y, 0.0, 1.0 );
        float diffuse = clamp( dot( nor, light_dir ), 0.0, 1.0 );
        float back_light = clamp( dot( nor, normalize(vec3(-light_dir.x,0.0,-light_dir.z))), 0.0, 1.0 )*clamp( 1.0-pos.y,0.0,1.0);

        // TODO: turn back on shadows
        diffuse *= calcSoftshadow( pos, light_dir, 0.02, 2.5 );

        float spe = pow( clamp( dot( nor, hal ), 0.0, 1.0 ),16.0)*
                    diffuse *
                    (0.04 + 0.96*pow( clamp(1.0+dot(hal,rd),0.0,1.0), 5.0 ));

        vec3 lin = vec3(0.0);
        lin += 1.30*diffuse*vec3(1.00,0.80,0.55);
        lin += 0.40*ambient*vec3(0.40,0.60,1.00);//*occ;
        lin += 0.50*back_light*vec3(0.25,0.25,0.25);//*occ;
        color = color*lin;
        color += 10.00*spe*vec3(1.00,0.90,0.70);
        #endif
    }

    return vec3( clamp(color,0.0,1.0) );
}

mat3 setCamera( in vec3 ro, in vec3 ta, float cr )
{
    vec3 cw = normalize(ta-ro);
    vec3 cp = vec3(sin(cr), cos(cr),0.0);
    vec3 cu = normalize( cross(cw,cp) );
    vec3 cv = normalize( cross(cu,cw) );
    return mat3( cu, cv, cw );
}

// plane.xyz must be normalized
float planeIntersect( in vec3 ro, in vec3 rd, in vec4 plane )  {
    return -(dot(ro,plane.xyz)+plane.w)/dot(rd,plane.xyz);
}

void main()
{
    vec3 tot = vec3(0.0);
// TODO:  turn back on AA
#define AA 1
#if AA>1
    for( int m=0; m<AA; m++ )
    for( int n=0; n<AA; n++ )
    {
        // pixel coordinates
        vec2 o = vec2(float(m),float(n)) / float(AA) - 0.5;
        vec2 p = (-resolution.xy + 2.0*(gl_FragCoord.xy+o))/resolution.y;
#else
        vec2 p = (-resolution.xy + 2.0*gl_FragCoord.xy)/resolution.y;
#endif

        vec3 ro = viewEye;
        vec3 ta = viewCenter;

        mat3 camera_to_world = setCamera( ro, ta, 0.0 );
        vec3 ray_direction = camera_to_world * normalize( vec3(p.xy,2.0) );

        vec3 col = render( ro, ray_direction );

        col = pow( col, vec3(0.4545) ); // gamma
        
        if (visualizer > 0.) {
            float dist = planeIntersect(ro, ray_direction, vec4(0,0,1.,0));
            if (dist > 0.) {
                vec3 t = ro + dist*ray_direction;
                float sdf_value = signed_distance_field(t).x;
                vec4 field_color = (sdf_value < 0. ? 
                                    vec4(1.,0.,0., sin(sdf_value*8.+runTime*2.)/4. + 0.25): 
                                    vec4(0.15, 0.15,0.8,sin(sdf_value*8.-runTime*2.)/4. + 0.25 )) ;

                col = mix(col, field_color.rgb, field_color.a);
            }
        }

        tot += col;
#if AA>1
    }
    tot /= float(AA*AA);
#endif

    finalColor = vec4( tot, 1.0 );
}
