/* gfx.c - math, shaders, meshes, instanced renderer, post-processing and HUD primitives.
 * Part of Port Calder; included by main.c (single translation unit, plus audio.c). */

/* ================================================================== math */
typedef struct { float x, y, z; } v3;
typedef struct { float m[16]; } m4;
static v3 V(float x, float y, float z) { return (v3){x, y, z}; }
static v3 add(v3 a, v3 b) { return V(a.x + b.x, a.y + b.y, a.z + b.z); }
static v3 sub(v3 a, v3 b) { return V(a.x - b.x, a.y - b.y, a.z - b.z); }
static v3 mul(v3 a, float s) { return V(a.x * s, a.y * s, a.z * s); }
static float dot(v3 a, v3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static v3 cross(v3 a, v3 b) { return V(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static float len(v3 a) { return sqrtf(dot(a, a)); }
static float len2d(v3 a) { return sqrtf(a.x * a.x + a.z * a.z); }
static float dist2d(v3 a, v3 b) { return len2d(sub(a, b)); }
static v3 norm(v3 a) { float l = len(a); return l > 1e-8f ? mul(a, 1 / l) : V(0, 1, 0); }
static v3 lerp3(v3 a, v3 b, float t) { return add(a, mul(sub(b, a), t)); }
static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static float smooth01(float a, float b, float x) { float t = clampf((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t); }
static float frand(void) { return (float)rand() / (float)RAND_MAX; }
static float frs(void) { return frand() * 2 - 1; }
static float wrap_pi(float a) { while (a > 3.14159265f) a -= 6.2831853f; while (a < -3.14159265f) a += 6.2831853f; return a; }
static v3 fwd_of(float yaw) { return V(sinf(yaw), 0, -cosf(yaw)); }
static v3 right_of(float yaw) { return V(cosf(yaw), 0, sinf(yaw)); }
static float yaw_of(v3 d) { return atan2f(d.x, -d.z); }
/* deterministic hash for world generation */
static unsigned hashu(unsigned x) { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x; }
static float hash2f(int a, int b, int c) { return (hashu((unsigned)a * 73856093U ^ (unsigned)b * 19349663U ^ (unsigned)c * 83492791U) & 0xffffff) / 16777216.0f; }

static m4 m4_id(void) { m4 r = {{0}}; r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1; return r; }
static m4 m4_mul(m4 a, m4 b) {
    m4 r;
    for (int c = 0; c < 4; c++)
        for (int rr = 0; rr < 4; rr++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += a.m[k * 4 + rr] * b.m[c * 4 + k];
            r.m[c * 4 + rr] = s;
        }
    return r;
}
static m4 m4_persp(float fovy, float asp, float n, float f) {
    float t = 1 / tanf(fovy / 2);
    m4 r = {{0}};
    r.m[0] = t / asp; r.m[5] = t; r.m[10] = (f + n) / (n - f); r.m[11] = -1; r.m[14] = 2 * f * n / (n - f);
    return r;
}
static m4 m4_ortho(float l, float r_, float b, float t, float n, float f) {
    m4 o = m4_id();
    o.m[0] = 2 / (r_ - l); o.m[5] = 2 / (t - b); o.m[10] = -2 / (f - n);
    o.m[12] = -(r_ + l) / (r_ - l); o.m[13] = -(t + b) / (t - b); o.m[14] = -(f + n) / (f - n);
    return o;
}
static m4 m4_look(v3 eye, v3 at, v3 up) {
    v3 f = norm(sub(at, eye)), s = norm(cross(f, up)), u = cross(s, f);
    m4 r = m4_id();
    r.m[0] = s.x; r.m[4] = s.y; r.m[8] = s.z;
    r.m[1] = u.x; r.m[5] = u.y; r.m[9] = u.z;
    r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
    r.m[12] = -dot(s, eye); r.m[13] = -dot(u, eye); r.m[14] = dot(f, eye);
    return r;
}
/* T * Ry(yaw) * Rx(pitch) * Rz(roll) * S ; yaw 0 faces -z, positive yaw turns toward +x */
static m4 m4_trs(v3 t, float yaw, float pitch, float roll, v3 s) {
    float cy = cosf(-yaw), sy = sinf(-yaw), cp = cosf(pitch), sp = sinf(pitch), cr = cosf(roll), sr = sinf(roll);
    float R[9] = {cy * cr + sy * sp * sr, cp * sr, -sy * cr + cy * sp * sr,
                  -cy * sr + sy * sp * cr, cp * cr, sy * sr + cy * sp * cr,
                  sy * cp, -sp, cy * cp};
    m4 m = m4_id();
    m.m[0] = R[0] * s.x; m.m[1] = R[1] * s.x; m.m[2] = R[2] * s.x;
    m.m[4] = R[3] * s.y; m.m[5] = R[4] * s.y; m.m[6] = R[5] * s.y;
    m.m[8] = R[6] * s.z; m.m[9] = R[7] * s.z; m.m[10] = R[8] * s.z;
    m.m[12] = t.x; m.m[13] = t.y; m.m[14] = t.z;
    return m;
}
static m4 m4_box(v3 c, v3 s) { return m4_trs(c, 0, 0, 0, s); }
static m4 m4_inv(m4 a) {
    const float *m = a.m;
    float inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    m4 r;
    det = fabsf(det) > 1e-12f ? 1.0f / det : 0;
    for (int i = 0; i < 16; i++) r.m[i] = inv[i] * det;
    return r;
}
static v3 m4_point(m4 a, v3 p) {
    return V(a.m[0] * p.x + a.m[4] * p.y + a.m[8] * p.z + a.m[12], a.m[1] * p.x + a.m[5] * p.y + a.m[9] * p.z + a.m[13],
             a.m[2] * p.x + a.m[6] * p.y + a.m[10] * p.z + a.m[14]);
}

/* ================================================================== shaders */
#define GLSL_COMMON                                                                                   \
    "float hash13(vec3 p){p=fract(p*0.1031);p+=dot(p,p.zyx+31.32);return fract((p.x+p.y)*p.z);}\n"   \
    "float hash12(vec2 p){vec3 q=fract(vec3(p.xyx)*0.1031);q+=dot(q,q.yzx+33.33);return fract((q.x+q.y)*q.z);}\n" \
    "float vnoise(vec3 p){vec3 i=floor(p),f=fract(p);f=f*f*(3.0-2.0*f);\n"                           \
    " return mix(mix(mix(hash13(i),hash13(i+vec3(1,0,0)),f.x),mix(hash13(i+vec3(0,1,0)),hash13(i+vec3(1,1,0)),f.x),f.y),\n" \
    "            mix(mix(hash13(i+vec3(0,0,1)),hash13(i+vec3(1,0,1)),f.x),mix(hash13(i+vec3(0,1,1)),hash13(i+vec3(1,1,1)),f.x),f.y),f.z);}\n" \
    "float fbmn(vec2 p){float a=0.5,t=0.0; for(int i=0;i<4;i++){t+=a*vnoise(vec3(p,float(i)*7.31)); p=mat2(1.6,1.2,-1.2,1.6)*p; a*=0.5;} return t/0.9375;}\n" \
    "uniform vec3 uSunDir; uniform float uNight, uRain, uTime;\n"                                      \
    "vec3 skyBase(vec3 d){\n"                                                                          \
    " float h=d.y, sh=uSunDir.y; float day=smoothstep(-0.16,0.14,sh); float dusk=exp(-pow(sh*4.0,2.0));\n" \
    " vec3 zen=mix(vec3(0.004,0.006,0.020),vec3(0.10,0.24,0.62),day);\n"                              \
    " vec3 hor=mix(vec3(0.035,0.035,0.060),vec3(0.55,0.66,0.82),day);\n"                              \
    " vec3 sd=normalize(vec3(uSunDir.x,0.0,uSunDir.z)+1e-4); float side=max(dot(normalize(vec3(d.x,0.0,d.z)+1e-4),sd),0.0);\n" \
    " hor=mix(hor,vec3(1.0,0.42,0.18),dusk*(0.35+0.55*side));\n"                                      \
    " vec3 c=mix(hor,zen,pow(clamp(h,0.0,1.0),0.45));\n"                                               \
    " c+=vec3(0.16,0.07,0.03)*exp(-max(h,0.0)*7.0)*uNight;\n"                                          \
    " float s=max(dot(d,uSunDir),0.0); c+=vec3(1.0,0.6,0.3)*pow(s,8.0)*(0.15+dusk*0.9)*day; c+=vec3(1.0,0.85,0.6)*pow(s,180.0)*1.5*day;\n" \
    " if(h<0.0) c=mix(c,hor*0.4,clamp(-h*3.0,0.0,1.0));\n"                                             \
    " float g=dot(c,vec3(0.3,0.5,0.2)); c=mix(c,vec3(g)*vec3(0.85,0.9,1.0)*0.75,uRain*0.75);\n"        \
    " return c*1.4;}\n"                                                                                \
    "vec3 skyFull(vec3 d){\n"                                                                          \
    " vec3 c=skyBase(d); float h=d.y; float day=smoothstep(-0.16,0.14,uSunDir.y);\n"                  \
    " float s=dot(d,uSunDir); c+=vec3(1.0,0.9,0.75)*smoothstep(0.9992,0.9995,s)*30.0*(1.0-uRain);\n"  \
    " vec3 md=normalize(vec3(-uSunDir.x,-uSunDir.y,uSunDir.z*0.6+0.25)); float m=dot(d,md);\n"         \
    " c+=vec3(0.75,0.8,0.9)*smoothstep(0.99955,0.9997,m)*3.0*(1.0-day)*(1.0-uRain);\n"                  \
    " vec3 p=d*300.0; vec3 id=floor(p); float r=hash13(id);\n"                                        \
    " if(r>0.997&&h>0.03){float k=smoothstep(0.45,0.0,length(fract(p)-0.5)); c+=vec3(0.9,0.95,1.0)*k*(r-0.997)*700.0*(1.0-day)*(1.0-uRain)*clamp(h*4.0,0.0,1.0);}\n" \
    " if(h>0.0){ vec2 uv=d.xz/(h+0.08)*0.8+vec2(uTime*0.004,uTime*0.002);\n" \
    "  vec2 w=vec2(fbmn(uv*0.7+3.0),fbmn(uv*0.7+9.0))-0.5; float n=fbmn(uv*1.2+w*1.6);\n" \
    "  float cov=mix(0.56,0.30,uRain); float cl=smoothstep(cov,cov+0.22,n)*smoothstep(0.0,0.12,h);\n" \
    "  vec2 sdir=normalize(uSunDir.xz+1e-4)*0.25; float lightn=fbmn(uv*1.2+w*1.6+sdir); float thick=clamp((lightn-n)*4.0+0.5,0.0,1.0);\n" \
    "  vec3 lit=mix(vec3(0.05,0.045,0.05)+vec3(0.10,0.05,0.025)*uNight,vec3(1.0,0.97,0.95),day);\n" \
    "  vec3 dark=mix(vec3(0.03,0.03,0.04),vec3(0.55,0.60,0.70),day);\n" \
    "  lit=mix(lit,vec3(1.0,0.55,0.35),exp(-pow(uSunDir.y*4.0,2.0))*0.6*day);\n" \
    "  vec3 cc=mix(lit,dark,(1.0-thick)*0.6+smoothstep(cov+0.1,cov+0.5,n)*0.25);\n" \
    "  cc+=vec3(1.0,0.85,0.6)*pow(max(s,0.0),12.0)*(1.0-cl)*0.8*day;\n" \
    "  cc*=mix(1.0,0.45,uRain)*(0.8+0.2*pow(max(s,0.0),3.0));\n" \
    "  vec2 cu=d.xz/(h+0.03)*0.35+vec2(uTime*0.002,0.0); float ci=fbmn(vec2(cu.x*0.6,cu.y*3.0)+fbmn(cu*2.0)); ci=smoothstep(0.55,0.8,ci)*smoothstep(0.02,0.3,h)*(1.0-uRain);\n" \
    "  c=mix(c,mix(lit,c,0.35)*1.15,ci*0.45);\n" \
    "  c=mix(c,cc*1.2,cl*0.92*smoothstep(0.0,0.05,h)); }\n" \
    " float az=atan(d.z,d.x); float inland=smoothstep(-0.35,0.25,d.x*0.85-d.z*0.5);\n" \
    " float r1=0.010+0.030*fbmn(vec2(az*3.0,1.0))+0.006*vnoise(vec3(az*40.0,0.0,0.0));\n" \
    " float r2=0.004+0.016*fbmn(vec2(az*5.0,7.0))+0.004*vnoise(vec3(az*70.0,3.0,0.0));\n" \
    " vec3 hz=skyBase(vec3(d.x,0.0,d.z)); vec3 m1=mix(hz,vec3(0.30,0.36,0.48)*(0.25+0.75*day),0.30); vec3 m2=mix(hz,vec3(0.22,0.27,0.34)*(0.2+0.8*day),0.48);\n" \
    " float snowcap=smoothstep(r1-0.006,r1-0.001,h)*step(0.032,r1)*day; m1=mix(m1,hz*1.15,snowcap*0.6);\n" \
    " if(h>-0.02){ c=mix(c,m1,inland*(1.0-uRain*0.7)*smoothstep(r1+0.0015,r1,h));\n" \
    "  c=mix(c,m2,inland*(1.0-uRain*0.7)*smoothstep(r2+0.0015,r2,h)); }\n" \
    " return c;}\n"

/* every lit object is an instance: model matrix + base(rgb,pattern) + mat(metal,rough,seed,nightEmis) + emis */
static const char *VS_LIT =
    "layout(location=0) in vec3 aPos; layout(location=1) in vec3 aNrm;\n"
    "layout(location=2) in vec4 iM0; layout(location=3) in vec4 iM1; layout(location=4) in vec4 iM2; layout(location=5) in vec4 iM3;\n"
    "layout(location=6) in vec4 iBase; layout(location=7) in vec4 iMat; layout(location=8) in vec4 iEmis;\n"
    "uniform mat4 uVP;\n"
    "out vec3 vPos, vNrm, vObj, vONrm, vSize; flat out vec4 vBase, vMat, vEmis;\n"
    "void main(){ mat4 M=mat4(iM0,iM1,iM2,iM3); vec4 w=M*vec4(aPos,1.0); vPos=w.xyz;\n"
    " vNrm=transpose(inverse(mat3(M)))*aNrm; vObj=aPos; vONrm=aNrm; vSize=vec3(length(iM0.xyz),length(iM1.xyz),length(iM2.xyz));\n"
    " vBase=iBase; vMat=iMat; vEmis=iEmis; gl_Position=uVP*w; }\n";

static const char *FS_LIT =
    "in vec3 vPos, vNrm, vObj, vONrm, vSize; flat in vec4 vBase, vMat, vEmis; out vec4 frag;\n"
    "uniform vec3 uCam, uLightDir, uLightCol, uAmbSky, uAmbGnd; uniform float uFogDen, uWater;\n"
    "uniform int uNumLights; uniform vec4 uLightPos[32]; uniform vec3 uLightCols[32];\n"
    "uniform mat4 uLightVP; uniform sampler2DShadow uShadow;\n"
    "const float PI=3.14159265;\n"
    "float D_GGX(float nh,float a){float a2=a*a; float d=nh*nh*(a2-1.0)+1.0; return a2/(PI*d*d);}\n"
    "float G_S(float nv,float nl,float r){float k=(r+1.0)*(r+1.0)/8.0; return (nv/(nv*(1.0-k)+k))*(nl/(nl*(1.0-k)+k));}\n"
    "vec3 shade(vec3 N,vec3 V,vec3 L,vec3 alb,float met,float rough,vec3 rad){\n"
    " vec3 H=normalize(V+L); float nl=max(dot(N,L),0.0), nv=max(dot(N,V),1e-3), nh=max(dot(N,H),0.0);\n"
    " vec3 f0=mix(vec3(0.04),alb,met); vec3 F=f0+(1.0-f0)*pow(1.0-max(dot(H,V),0.0),5.0);\n"
    " vec3 spec=D_GGX(nh,max(rough*rough,0.002))*G_S(nv,nl,rough)*F/(4.0*nv*max(nl,1e-3)+1e-4);\n"
    " vec3 kd=(1.0-F)*(1.0-met); return (kd*alb/PI+spec)*rad*nl;}\n"
    "float shadowAt(vec3 N){ vec4 s=uLightVP*vec4(vPos+N*0.18,1.0); vec3 p=s.xyz/s.w*0.5+0.5;\n"
    " if(p.z>1.0||p.x<0.0||p.x>1.0||p.y<0.0||p.y>1.0) return 1.0;\n"
    " float e=smoothstep(0.42,0.5,max(abs(p.x-0.5),abs(p.y-0.5)));\n"
    " float sum=0.0; vec2 t=1.0/vec2(textureSize(uShadow,0));\n"
    " for(int y=-1;y<=1;y++) for(int x=-1;x<=1;x++) sum+=texture(uShadow,vec3(p.xy+vec2(x,y)*t,p.z-0.0006));\n"
    " return mix(sum/9.0,1.0,e);}\n"
    "uniform sampler2D uBiome; uniform vec2 uMapMin, uMapSize; uniform float uSnow;\n"
    /* the land: biomes from a low-res map, rock on slopes, snow on peaks, beaches at the water */
    "vec3 fields(vec2 q,float dist,out float trk){\n"
    /* farm parcels of mixed size: big blocks split into strips, gently warped edges, rows in varying directions */
    "  vec2 fq=q+(vec2(fbmn(q*0.0025),fbmn(q*0.0025+5.0))-0.5)*140.0; float B=230.0;\n"
    "  vec2 big=floor(fq/B), fb=fract(fq/B); float hb=hash12(big); bool ax=hash12(big+3.1)>0.5;\n"
    "  float ns=1.0+floor(hb*3.99); float co=ax?fb.x:fb.y; float sid=floor(co*ns), sf=fract(co*ns);\n"
    "  vec2 cell=big*7.0+vec2(sid,ax?1.0:2.0); float hc=hash12(cell), hr=hash12(cell+11.0);\n"
    "  float edge=min(min(min(fb.x,1.0-fb.x),min(fb.y,1.0-fb.y))*B,min(sf,1.0-sf)*B/ns);\n"
    "  vec2 rd=hr<0.4?vec2(1.0,0.0):hr<0.8?vec2(0.0,1.0):vec2(0.7071,0.7071); float rf=1.8+hash12(cell+4.0)*1.6;\n"
    "  float rows=0.5+0.5*sin(dot(q,rd)*rf); rows=mix(rows,0.5,smoothstep(60.0,220.0,dist));\n"
    "  float moist=fbmn(q*0.03+hc*20.0);\n"
    "  vec3 crop;\n"
    "  if(hc<0.22) crop=mix(vec3(0.13,0.26,0.05),vec3(0.20,0.33,0.07),moist)*(0.78+0.32*rows);\n"        /* row crops */
    "  else if(hc<0.40) crop=mix(vec3(0.62,0.52,0.24),vec3(0.52,0.42,0.18),moist)*(0.9+0.15*rows);\n"    /* wheat */
    "  else if(hc<0.55) crop=mix(vec3(0.36,0.25,0.15),vec3(0.27,0.19,0.12),moist)*(0.78+0.3*rows);\n"    /* ploughed */
    "  else if(hc<0.70) crop=mix(vec3(0.22,0.38,0.08),vec3(0.30,0.44,0.12),moist);\n"                    /* alfalfa */
    "  else if(hc<0.82) crop=mix(vec3(0.46,0.40,0.26),vec3(0.38,0.34,0.22),moist);\n"                    /* fallow */
    "  else { vec2 t=fract(q/7.0)-0.5; float tree=smoothstep(0.32,0.22,length(t)); crop=mix(vec3(0.33,0.30,0.18),vec3(0.08,0.15,0.04),tree*smoothstep(260.0,90.0,dist)+0.35*smoothstep(90.0,260.0,dist)); }\n" /* orchard */
    "  if(hash12(big+9.0)>0.86){ float r=length(fb-0.5)*B; crop=mix(crop,mix(vec3(0.16,0.32,0.06),vec3(0.24,0.38,0.09),moist)*(0.9+0.1*sin(r*1.4)),smoothstep(B*0.47,B*0.45,r)); }\n" /* centre pivots */
    "  trk=smoothstep(3.5,1.5,edge); return crop; }\n"
    "void terrain(inout vec3 alb,inout float rough,inout float met,vec3 N){\n"
    " vec2 q=vPos.xz; float h=vPos.y; float dist=length(uCam-vPos);\n"
    " vec2 wq=q+(vec2(vnoise(vec3(q*0.006,1.0)),vnoise(vec3(q*0.006,9.0)))-0.5)*50.0;\n"
    " vec4 b=texture(uBiome,(wq-uMapMin)/uMapSize);\n"
    " float m1=fbmn(q*0.0009), m2=fbmn(q*0.005+3.1), d1=fbmn(q*0.045+1.7);\n"
    " float n1=vnoise(vec3(q*0.9,0.0)), n3=vnoise(vec3(q*3.3,2.0)), n4=vnoise(vec3(q*0.25,5.0));\n"
    " float fine=smoothstep(140.0,30.0,dist);\n"
    " vec3 lush=vec3(0.07,0.17,0.04), green=vec3(0.12,0.21,0.06), golden=vec3(0.47,0.37,0.17), dry=vec3(0.40,0.34,0.21), desert=vec3(0.66,0.52,0.36), forest=vec3(0.05,0.09,0.035), sand=vec3(0.70,0.60,0.43), dirt=vec3(0.31,0.25,0.17);\n"
    " float ar=b.r+(m1-0.5)*0.35+(m2-0.5)*0.18;\n"
    " vec3 g=mix(lush,green,m2); g=mix(g,golden,smoothstep(0.3,0.62,ar)); g=mix(g,dry,smoothstep(0.5,0.8,ar)*(1.0-m2)*0.7); g=mix(g,desert,smoothstep(0.78,0.92,ar));\n"
    " g=mix(g,forest*(0.75+0.5*d1),clamp(b.b*0.95+(m2-0.5)*0.6,0.0,1.0));\n"
    " float fo=clamp(b.b*0.95+(m2-0.5)*0.6,0.0,1.0); float duff=smoothstep(0.32,0.62,fbmn(q*0.07+2.0)); g=mix(g,vec3(0.14,0.10,0.06)*(0.75+0.5*n4),max(fo,0.45*(1.0-smoothstep(0.2,0.4,ar)))*duff*0.8);\n"
    " g=mix(g,vec3(0.03,0.06,0.02),fo*smoothstep(0.6,0.8,vnoise(vec3(q*0.4,12.0)))*0.6*fine);\n"
    /* bare patches, scrub and bushes break up the open ground */
    " float pt=smoothstep(0.60,0.74,fbmn(q*0.018+7.0)); g=mix(g,mix(dirt,desert,smoothstep(0.6,0.9,ar))*(0.9+0.2*n4),pt*0.55*(1.0-b.b));\n"
    " vec3 shrub=mix(vec3(0.07,0.12,0.04),vec3(0.20,0.20,0.10),smoothstep(0.75,0.95,ar));\n"
    " vec2 bc=floor(q/2.6); vec2 bf=fract(q/2.6)-0.5+(vec2(hash12(bc),hash12(bc+5.0))-0.5)*0.5; float bsz=0.18+0.2*hash12(bc+9.0);\n"
    " float bush=smoothstep(bsz,bsz*0.6,length(bf))*step(hash12(bc+2.0),0.15+0.5*smoothstep(0.35,0.75,d1))*(0.7+0.3*hash12(bc+4.0)); bush=mix(bush,0.06+0.16*smoothstep(0.35,0.75,d1),smoothstep(25.0,90.0,dist)); g=mix(g,shrub,bush*0.75*(1.0-b.b));\n"
    /* California hills: golden grass facing the sun, dark chaparral and oak on north-facing slopes */
    " float nface=smoothstep(0.02,0.25,-N.z)*smoothstep(0.08,0.2,1.0-N.y)*smoothstep(0.3,0.5,ar)*(1.0-smoothstep(0.82,0.94,ar));\n"
    " g=mix(g,vec3(0.08,0.13,0.05)*(0.75+0.5*d1),nface*clamp(0.55+(d1-0.5)*1.2,0.0,0.9)*(1.0-b.b));\n"
    " float streak=vnoise(vec3(q.x*0.7,q.y*0.18,8.0))*fine; g*=0.92+0.16*streak;\n"
    " if(b.g>0.03){ float trk; vec3 crop=fields(q,dist,trk); crop=mix(crop,vec3(0.36,0.31,0.22),trk); g=mix(g,crop,smoothstep(0.0,0.5,b.g)); }\n"
    " if(b.a>0.1&&b.a<0.4){ vec2 vq=q+vec2(0.0,fbmn(q*0.004)*60.0); float rows=smoothstep(0.35,0.65,0.5+0.5*sin(vq.y*1.6)); rows=mix(rows,0.45,smoothstep(80.0,250.0,dist)); g=mix(g,mix(vec3(0.38,0.30,0.20),vec3(0.16,0.26,0.07),rows),0.8); }\n"
    " if(b.a>0.4&&b.a<0.6){ float cr=smoothstep(0.03,0.0,abs(vnoise(vec3(q*0.12,6.0))-0.5))*fine; g=mix(g,vec3(0.90,0.88,0.82)*(0.92+0.1*n1)*(1.0-0.25*cr),0.85); }\n"
    " if(b.a>0.6){ float dn=sin(q.x*0.11+q.y*0.045+fbmn(q*0.004)*14.0); g=mix(g,vec3(0.80,0.64,0.44)*(0.88+0.14*dn+0.05*n1),0.9); }\n"
    " alb=g*(0.86+0.24*n1*mix(0.5,1.0,fine))*(0.94+0.12*n3*fine)*(0.88+0.24*m2); rough=0.93; met=0.0;\n"
    /* rock: weathered, warped layers, no regular stripes */
    " vec3 P=vPos; float rn=vnoise(P*0.07)*0.5+vnoise(P*0.29)*0.3+vnoise(P*1.2)*0.2*fine;\n"
    " float lay=0.5+0.5*sin(h*0.22+rn*7.0+m2*12.0); float crack=smoothstep(0.02,0.10,abs(vnoise(P*0.45)-0.5));\n"
    " float slope=1.0-N.y; float rk=smoothstep(0.28,0.50,slope+(rn-0.5)*0.22+b.r*0.06)*(1.0-0.65*smoothstep(0.2,0.7,b.b)*(1.0-smoothstep(0.55,0.75,slope)));\n"
    " vec3 rc=mix(vec3(0.27,0.26,0.25),vec3(0.50,0.47,0.43),rn)*(0.9+0.14*lay)*(0.72+0.28*crack);\n"
    " rc=mix(rc,rc*vec3(1.18,0.94,0.76),smoothstep(0.6,0.9,b.r)); rc=mix(rc,rc*vec3(0.92,1.0,0.92),(1.0-smoothstep(0.2,0.5,b.r))*0.6);\n"
    " alb=mix(alb,rc,rk); rough=mix(rough,0.8,rk);\n"
    " float sn=smoothstep(uSnow-25.0,uSnow+10.0,h+(m2-0.5)*60.0+(n4-0.5)*10.0)*smoothstep(0.45,0.75,N.y+(rn-0.5)*0.2); alb=mix(alb,vec3(0.90,0.92,0.95)*(0.94+0.06*n1),sn); rough=mix(rough,0.45,sn);\n"
    " float beach=(1.0-smoothstep(1.4,3.2,h-uWater+(n4-0.5)*0.8))*step(h,6.0); alb=mix(alb,sand*(0.9+0.15*n1+0.05*n3),beach);\n"
    " if(h<uWater+0.15) alb*=0.6;\n"
    " float wet=uRain*0.6*(0.5+0.5*n4); rough=mix(rough,0.3,wet); alb*=1.0-0.3*wet; }\n"
    /* roads: the vertex 'normal' carries (across, along, type + length/1000) */
    "void roadsurf(inout vec3 alb,inout float rough,inout float met,inout vec3 emis){\n"
    " float u=vONrm.x, v=vONrm.y; int type=int(floor(vONrm.z)); float L=fract(vONrm.z)*1000.0; float au=abs(u);\n"
    " vec2 q=vPos.xz; float n1=vnoise(vec3(q*0.9,0.0)), n2=vnoise(vec3(q*0.11,3.0));\n"
    " alb=vec3(0.055)*(0.8+0.3*n1)*(0.85+0.3*n2); rough=0.82; met=0.0; vec3 mark=vec3(0.0);\n"
    " if(type==0){ float tr=exp(-pow((abs(au-2.2)-0.85)*3.2,2.0)); alb*=1.0-0.22*tr; rough=mix(rough,0.55,tr*0.6);\n"
    "  if(au>0.1&&au<0.26) mark=vec3(0.78,0.58,0.1); if(au>4.5&&au<4.65) mark=vec3(0.78);\n"
    "  float e=min(v,L-v); if(e>0.6&&e<3.6&&fract(u/1.2)<0.5&&au<4.5) mark=vec3(0.8);\n"
    "  if(e>3.8&&e<4.2&&u>0.0&&v>L*0.5) mark=vec3(0.8); if(e>3.8&&e<4.2&&u<0.0&&v<L*0.5) mark=vec3(0.8);\n"
    "  vec2 mh=vec2(au-2.2,mod(v,31.0)-15.5); if(length(mh)<0.45){ alb=vec3(0.09,0.085,0.08)*(0.7+0.6*step(0.5,fract((mh.x+mh.y)*4.0))); rough=0.45; met=0.6; } }\n"
    " else if(type==1){ float tr=exp(-pow((abs(au-1.85)-0.8)*3.0,2.0))+exp(-pow((abs(au-5.45)-0.8)*3.0,2.0)); alb*=1.0-0.18*tr;\n"
    "  if(au>0.1&&au<0.28) mark=vec3(0.80,0.60,0.1); if(au>3.55&&au<3.72&&fract(v/12.0)<0.45) mark=vec3(0.82); if(au>7.05&&au<7.25) mark=vec3(0.82);\n"
    "  if(au>7.3){ alb=vec3(0.09)*(0.8+0.3*n1); } }\n"
    " else { float e=max(abs(u),abs(v)); alb*=0.95; }\n"
    " vec2 pc=floor(q/7.0); if(hash12(pc)>0.86) alb*=0.78+0.12*hash12(pc+3.0);\n"
    " float cr=vnoise(vec3(q*0.45,9.0)); if(abs(cr-0.5)<0.012&&vnoise(vec3(q*0.07,4.0))>0.55) alb*=0.45;\n"
    " if(dot(mark,mark)>0.0){ alb=mark*(0.7+0.3*n1)*(0.75+0.25*vnoise(vec3(q*2.0,1.0))); rough=0.6; }\n"
    " float wet=uRain*(0.45+0.55*smoothstep(0.35,0.7,vnoise(vec3(q*0.12,0.5)))); rough=mix(rough,0.06,wet); alb*=1.0-0.45*wet;\n"
    " if(type==2&&uNight>0.01){ float acc=0.0; for(int k=0;k<4;k++){ vec2 o=vec2((k&1)==0?-7.6:7.6,(k&2)==0?-7.6:7.6); vec2 d=vec2(u,v)-o; acc+=exp(-dot(d,d)/42.0);} emis+=alb*vec3(1.0,0.68,0.36)*acc*uNight*7.0; } }\n"
    /* the kerbed slab under every city block: sidewalk ring + the lot */
    "void slab(inout vec3 alb,inout float rough,inout float met,inout vec3 emis,int lot){\n"
    " vec2 q=vPos.xz; float n1=vnoise(vec3(q*0.9,0.0)), n2=vnoise(vec3(q*0.11,3.0)); met=0.0;\n"
    " if(abs(vONrm.y)<0.5){ alb=vec3(0.48,0.47,0.45)*(0.9+0.1*n1); rough=0.7; return; }\n"
    " float W=vSize.x, D=vSize.z; float u=(vObj.x+0.5)*W, v=(vObj.z+0.5)*D; float e=min(min(u,W-u),min(v,D-v));\n"
    " if(e<4.0){ vec2 t=abs(fract(q/1.8)-0.5); float seam=smoothstep(0.46,0.49,max(t.x,t.y)); alb=vec3(0.36,0.35,0.33)*(0.85+0.2*n2)*(1.0-0.35*seam); rough=0.85; if(e<0.3){ alb=vec3(0.52,0.51,0.49); rough=0.7; } }\n"
    " else if(lot==1||lot==4){ alb=vec3(0.09,0.18,0.045)*(0.7+0.5*n2)*(0.85+0.3*n1); rough=0.95;\n"
    "  if(lot==4){ vec2 bl=vec2(u,v)-vec2(W,D)*0.5; float pth=min(abs(bl.x-bl.y),abs(bl.x+bl.y))/1.414; if(pth<1.6||abs(length(bl)-14.0)<1.4){ alb=vec3(0.45,0.40,0.30)*(0.85+0.2*n1); rough=0.9; } } }\n"
    " else if(lot==2){ alb=vec3(0.07)*(0.8+0.3*n1); rough=0.8; if(fract(q.x/3.0)<0.04&&abs(fract(q.y/12.0)-0.5)<0.3) alb=vec3(0.7); }\n"
    " else if(lot==3){ alb=vec3(0.31,0.30,0.28)*(0.7+0.4*n2)*(0.9+0.15*n1); rough=0.9; }\n"
    " else if(lot==5){ alb=vec3(0.70,0.60,0.44)*(0.88+0.2*n1); rough=0.95; }\n"
    " else { vec2 t=abs(fract(q/3.0)-0.5); float seam=smoothstep(0.47,0.49,max(t.x,t.y)); alb=vec3(0.44,0.41,0.36)*(0.85+0.2*n2)*(1.0-0.3*seam); rough=0.75; }\n"
    " float wet=uRain*(0.45+0.55*smoothstep(0.35,0.7,vnoise(vec3(q*0.12,0.5)))); rough=mix(rough,0.1,wet); alb*=1.0-0.4*wet;\n"
    " if(uNight>0.01){ float acc=0.0; for(int k=0;k<4;k++){ vec2 c=vec2((k&1)==0?2.6:W-2.6,(k&2)==0?2.6:D-2.6); vec2 d=vec2(u,v)-c; acc+=exp(-dot(d,d)/42.0);} emis+=alb*vec3(1.0,0.68,0.36)*acc*uNight*7.0; } }\n"
    "void main(){\n"
    " vec3 N=normalize(vNrm); vec3 V=normalize(uCam-vPos); float dist=length(uCam-vPos);\n"
    " vec3 alb=vBase.rgb; int pat=int(vBase.w+0.5); float met=vMat.x, rough=vMat.y, seed=vMat.z;\n"
    " vec3 emis=vEmis.rgb*mix(1.0,uNight,vMat.w); float coat=0.0;\n"
    " vec3 an=abs(vONrm); bool side=an.y<0.5;\n"
    " float fu=an.x>0.5?(vObj.z+0.5)*vSize.z:(vObj.x+0.5)*vSize.x; float fv=(vObj.y+0.5)*vSize.y; float face=vONrm.x*3.0+vONrm.z*7.0;\n"
    " if(pat==1){ if(side){ vec2 cl=vec2(fu/2.6,fv/3.4); vec2 id=floor(cl); vec2 f=fract(cl);\n"
    "   float fr=max(step(f.x,0.07)+step(0.93,f.x),step(f.y,0.09)); bool top=fv>vSize.y-1.6;\n"
    "   if(fr>0.5||top){ alb=vBase.rgb*0.8; rough=0.45; met=0.6; }\n"
    "   else { alb=vec3(0.04,0.05,0.06)+vBase.rgb*0.06; met=0.85; rough=0.06+0.08*hash12(id+seed);\n"
    "    float h=hash13(vec3(id,seed*91.0+face)); if(h<0.42) emis+=mix(vec3(1.0,0.78,0.5),vec3(0.65,0.8,1.0),step(0.3,h))*(0.35+0.9*hash12(id*1.7+seed))*uNight*0.8; }\n"
    "   if(top&&seed>0.55) emis+=vec3(0.3,0.9,1.6)*uNight*1.3; }\n"
    "  else { alb=vec3(0.22); rough=0.9; met=0.0; } }\n"
    " else if(pat==2){ if(side){ vec2 cl=vec2(fu/3.2,(fv-4.4)/3.4); vec2 id=floor(cl); vec2 f=fract(cl);\n"
    "   alb*=0.85+0.25*vnoise(vPos*1.7); rough=0.85;\n"
    "   if(seed>0.33&&seed<0.66){ vec2 bk=vec2(fu/0.5,fv/0.17); bk.x+=0.5*mod(floor(bk.y),2.0); vec2 bf=fract(bk); float mort=max(step(bf.x,0.06),step(bf.y,0.12)); alb*=mix(0.85+0.3*hash12(floor(bk)),0.55,mort); }\n"
    "   else if(seed>=0.66){ vec2 pn=fract(vec2(fu/1.6,fv/0.8)); alb*=0.92+0.08*vnoise(vPos*4.0); alb*=1.0-0.25*max(step(pn.x,0.02),step(pn.y,0.04)); }\n"
    "   if(fv<4.4){ bool win=fv>0.4&&fv<3.4&&fract(fu/4.0)>0.12; if(win){ alb=vec3(0.03); met=0.8; rough=0.08; float hs=hash12(vec2(floor(fu/4.0),seed*31.0+face)); emis+=mix(vec3(1.0,0.7,0.4),vec3(0.75,0.85,1.0),step(0.6,hs))*0.55*uNight*(0.4+0.8*hs)*(0.75+0.25*smoothstep(0.4,3.4,fv)); }\n"
    "    else if(fv>3.4&&fv<4.1) { alb=vBase.rgb*0.4; } }\n"
    "   else if(fv<vSize.y-1.0&&abs(f.x-0.5)<0.28&&abs(f.y-0.5)<0.3){ alb=vec3(0.035,0.04,0.05); met=0.8; rough=0.08;\n"
    "    float h=hash13(vec3(id,seed*53.0+face)); if(h<0.38) emis+=mix(vec3(1.0,0.75,0.45),vec3(0.7,0.85,1.0),step(0.28,h))*(0.4+0.7*hash12(id+seed))*uNight*0.8; } }\n"
    "  else { alb=vec3(0.25); rough=0.9; } }\n"
    " else if(pat==3){ if(side){ alb*=0.9+0.15*vnoise(vPos*2.0); rough=0.9;\n"
    "   if(seed<0.3){ float bd=fract(fv/0.22); alb*=0.86+0.14*smoothstep(0.0,0.25,bd); }\n"
    "   else if(seed>=0.5){ alb*=0.93+0.1*vnoise(vPos*6.0); rough=0.95; }\n"
    "   float cx=fract(fu/4.0), fl=fv/2.9; vec2 id=vec2(floor(fu/4.0),floor(fl));\n"
    "   if(abs(cx-0.5)<0.2&&abs(fract(fl)-0.55)<0.22&&fv<vSize.y-0.3){ alb=vec3(0.05); met=0.6; rough=0.1;\n"
    "    if(hash13(vec3(id,seed*17.0+face))<0.55) emis+=vec3(1.0,0.72,0.4)*1.0*uNight; } }\n"
    "  else { alb=vec3(0.3); } }\n"
    " else if(pat==4){ if(seed>0.5){ float rw=fract(vPos.y*2.2); float bar=0.5+0.5*sin((vPos.x+vPos.z)*7.0); alb*=(0.72+0.28*smoothstep(0.0,0.2,rw))*(0.85+0.2*bar)*(0.9+0.2*hash12(floor(vPos.xz*2.0))); rough=0.65; }\n"
    "   else alb*=0.75+0.25*step(0.18,fract(vPos.y*3.0)); rough=max(rough,0.6); }\n"
    " else if(pat==5){ coat=1.0; }\n"
    " else if(pat==6){ emis*=0.85+0.15*sin(uTime*7.0+seed*40.0)*step(0.5,hash12(vec2(floor(uTime*9.0),seed))); }\n"
    " else if(pat==7){ vec2 q=vPos.xz; N=normalize(vec3((vnoise(vec3(q*0.8,uTime*0.7))-0.5)*0.15,1.0,(vnoise(vec3(q*0.8+5.0,uTime*0.7))-0.5)*0.15)); alb=vec3(0.02,0.06,0.07); rough=0.05; met=0.0; }\n"
    " else if(pat==8){ alb*=0.6+0.7*vnoise(vPos*1.6); rough=0.9; }\n"
    " else if(pat==9){ alb*=0.8+0.2*step(0.5,fract((side?fu:vPos.x)*2.5)); alb*=0.8+0.3*vnoise(vPos*0.8); rough=0.6; }\n"
    " else if(pat==10){ terrain(alb,rough,met,N); }\n"
    " else if(pat==13){ N=vec3(0.0,1.0,0.0); roadsurf(alb,rough,met,emis); }\n"
    " else if(pat==14){ slab(alb,rough,met,emis,int(seed)); }\n"
    " else if(pat==11){ vec2 q=vPos.xz; float t=uTime;\n"
    "   vec3 n=vec3(0.0,1.0,0.0); n.x+=0.06*sin(q.x*0.09+t*1.1)+0.04*sin(q.x*0.27+q.y*0.15+t*1.9)+(vnoise(vec3(q*0.5,t*0.5))-0.5)*0.16;\n"
    "   n.z+=0.06*sin(q.y*0.11+t*0.9)+0.04*sin(q.y*0.25-q.x*0.19+t*1.6)+(vnoise(vec3(q*0.5+7.0,t*0.5))-0.5)*0.16;\n"
    "   N=normalize(n); alb=vec3(0.008,0.04,0.05)*(1.0-0.5*uNight); rough=0.04+uRain*0.1; met=0.0; }\n"
    " else if(pat==12){ alb*=0.7+0.3*step(0.12,fract(vPos.x*1.2+vPos.z*0.01)); rough=0.85; }\n"
    " if(side&&(pat>=1&&pat<=3||pat==9)) alb*=mix(0.55,1.0,smoothstep(0.0,1.6,vPos.y));\n"
    " vec3 col=vec3(0.0);\n"
    " float sh=shadowAt(N); col+=shade(N,V,uLightDir,alb,met,rough,uLightCol)*sh;\n"
    " for(int i=0;i<uNumLights;i++){ vec3 d=uLightPos[i].xyz-vPos; float dd=length(d); float r=uLightPos[i].w;\n"
    "   if(dd<r){ float fall=1.0-pow(dd/r,4.0); fall=fall*fall/(dd*dd*0.25+1.0); col+=shade(N,V,d/dd,alb,met,rough,uLightCols[i]*fall); } }\n"
    " vec3 amb=mix(uAmbGnd,uAmbSky,N.y*0.5+0.5);\n"
    " vec3 f0=mix(vec3(0.04),alb,met); float nv=max(dot(N,V),0.0);\n"
    " vec3 F=f0+(max(vec3(1.0-rough),f0)-f0)*pow(1.0-nv,5.0);\n"
    " vec3 R=reflect(-V,N); if(R.y<0.0) R.y=-R.y*0.3; vec3 env=skyBase(R)*mix(1.0,0.12,rough)*(0.35+0.65*sh*(1.0-uNight)+uNight*0.6);\n"
    " col+=amb*alb*(1.0-met)*(0.55+0.45*sh)+env*F*(1.0-rough*0.6);\n"
    " if(coat>0.0){ float Fc=0.04+0.96*pow(1.0-nv,5.0); col+=skyBase(R)*Fc*0.7*(0.4+0.6*sh);\n"
    "   vec3 H=normalize(V+uLightDir); col+=uLightCol*D_GGX(max(dot(N,H),0.0),0.012)*0.04*Fc*sh*max(dot(N,uLightDir),0.0); }\n"
    " col+=emis;\n"
    " float fog=1.0-exp(-dist*uFogDen); fog*=clamp(exp(-max(vPos.y-20.0,0.0)*0.004),0.3,1.0);\n"
    " col=mix(col,skyBase(normalize(vec3(-V.x,max(-V.y,0.02)*0.3,-V.z)))*0.9,fog);\n"
    " frag=vec4(col,1.0);}\n";

static const char *VS_DEPTH =
    "layout(location=0) in vec3 aPos;\n"
    "layout(location=2) in vec4 iM0; layout(location=3) in vec4 iM1; layout(location=4) in vec4 iM2; layout(location=5) in vec4 iM3;\n"
    "uniform mat4 uLightVP; void main(){ gl_Position=uLightVP*mat4(iM0,iM1,iM2,iM3)*vec4(aPos,1.0); }\n";
static const char *FS_DEPTH = "out vec4 frag; void main(){ frag=vec4(1.0); }\n";

static const char *VS_FULL =
    "out vec2 vUV; void main(){ vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2); vUV=p; gl_Position=vec4(p*2.0-1.0,0.0,1.0); }\n";
static const char *FS_SKY =
    "in vec2 vUV; out vec4 frag; uniform mat4 uInvVP;\n"
    "void main(){ vec4 a=uInvVP*vec4(vUV*2.0-1.0,-1.0,1.0); vec4 b=uInvVP*vec4(vUV*2.0-1.0,1.0,1.0);\n"
    " vec3 d=normalize(b.xyz/b.w-a.xyz/a.w); frag=vec4(skyFull(d),1.0); }\n";

/* additive glows (kind 0 dot, 1 beam, 2 ring) and alpha smoke (kind 3), all premultiplied */
static const char *VS_PART =
    "layout(location=0) in vec3 aPos; layout(location=1) in vec3 aUV; layout(location=2) in vec4 aCol;\n"
    "uniform mat4 uVP; out vec3 vUV; out vec4 vCol;\n"
    "void main(){ gl_Position=uVP*vec4(aPos,1.0); vUV=aUV; vCol=aCol; }\n";
static const char *FS_PART =
    "in vec3 vUV; in vec4 vCol; out vec4 frag;\n"
    "void main(){ vec2 p=vUV.xy*2.0-1.0; float a;\n"
    " if(vUV.z<0.5){ a=max(0.0,1.0-dot(p,p)); a*=a; }\n"
    " else if(vUV.z<1.5){ a=max(0.0,1.0-abs(p.y)); a=a*a*a; }\n"
    " else if(vUV.z<2.5){ float r=length(p); a=smoothstep(1.0,0.82,r)*smoothstep(0.55,0.85,r); }\n"
    " else { a=max(0.0,1.0-dot(p,p)); a=a*a*vCol.a; frag=vec4(vCol.rgb*a,a); return; }\n"
    " frag=vec4(vCol.rgb*vCol.a*a,0.0); }\n";

static const char *FS_DOWN =
    "in vec2 vUV; out vec4 frag; uniform sampler2D uSrc; uniform vec2 uTexel; uniform int uFirst;\n"
    "void main(){ vec3 c=texture(uSrc,vUV+uTexel*vec2(-1,-1)).rgb+texture(uSrc,vUV+uTexel*vec2(1,-1)).rgb\n"
    " +texture(uSrc,vUV+uTexel*vec2(-1,1)).rgb+texture(uSrc,vUV+uTexel*vec2(1,1)).rgb; c*=0.25;\n"
    " if(uFirst==1){ float b=max(c.r,max(c.g,c.b)); float k=max(b-1.4,0.0); c*=k/max(b,1e-4); c=min(c,vec3(40.0)); }\n"
    " frag=vec4(c,1.0); }\n";
static const char *FS_UP =
    "in vec2 vUV; out vec4 frag; uniform sampler2D uSrc; uniform vec2 uTexel;\n"
    "void main(){ vec3 c=texture(uSrc,vUV).rgb*4.0;\n"
    " c+=(texture(uSrc,vUV+uTexel*vec2(-1,0)).rgb+texture(uSrc,vUV+uTexel*vec2(1,0)).rgb+texture(uSrc,vUV+uTexel*vec2(0,-1)).rgb+texture(uSrc,vUV+uTexel*vec2(0,1)).rgb)*2.0;\n"
    " c+=texture(uSrc,vUV+uTexel*vec2(-1,-1)).rgb+texture(uSrc,vUV+uTexel*vec2(1,-1)).rgb+texture(uSrc,vUV+uTexel*vec2(-1,1)).rgb+texture(uSrc,vUV+uTexel*vec2(1,1)).rgb;\n"
    " frag=vec4(c/16.0,1.0); }\n";
static const char *FS_COMP =
    "in vec2 vUV; out vec4 frag; uniform sampler2D uHdr, uBloom; uniform float uExposure, uDamage, uBloomAmt, uFade, uGray, uSpeed;\n"
    "vec3 aces(vec3 x){ return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14),0.0,1.0); }\n"
    "void main(){ vec2 cc=vUV-0.5; float ca=0.0008+uDamage*0.01+uSpeed*0.002;\n"
    " vec3 c=vec3(texture(uHdr,vUV+cc*ca).r,texture(uHdr,vUV).g,texture(uHdr,vUV-cc*ca).b);\n"
    " if(uSpeed>0.01){ vec3 acc=c; for(int i=1;i<6;i++){ acc+=texture(uHdr,vUV-cc*uSpeed*0.012*float(i)).rgb; } c=mix(c,acc/6.0,smoothstep(0.1,0.45,length(cc))); }\n"
    " c+=texture(uBloom,vUV).rgb*uBloomAmt; c*=uExposure; c=aces(c);\n"
    " float r=length(cc*vec2(1.0,0.75)); float vig=smoothstep(0.85,0.25,r); c*=mix(0.55,1.0,vig);\n"
    " c=mix(c,c*vec3(1.0,0.35,0.3)+vec3(0.3,0.0,0.02)*(1.0-vig),clamp(uDamage,0.0,1.0)*0.6);\n"
    " float g=dot(c,vec3(0.3,0.59,0.11)); c=mix(c,vec3(g)*vec3(1.05,0.97,0.9),uGray);\n"
    " c=pow(c,vec3(1.0/2.2)); c+=(hash12(gl_FragCoord.xy+fract(uTime)*100.0)-0.5)*0.022; c*=1.0-uFade;\n"
    " frag=vec4(c,1.0); }\n";

static const char *VS_HUD =
    "layout(location=0) in vec2 aPos; layout(location=1) in vec2 aUV; layout(location=2) in vec4 aCol;\n"
    "uniform vec2 uScreen; out vec2 vUV; out vec4 vCol;\n"
    "void main(){ gl_Position=vec4(aPos/uScreen*vec2(2.0,-2.0)+vec2(-1.0,1.0),0.0,1.0); vUV=aUV; vCol=aCol; }\n";
static const char *FS_HUD =
    "in vec2 vUV; in vec4 vCol; out vec4 frag; uniform sampler2D uFont; uniform int uMode;\n"
    "void main(){ if(uMode==1){ frag=texture(uFont,vUV)*vCol; return; } float a=vUV.x<0.0?1.0:texture(uFont,vUV).r; frag=vec4(vCol.rgb,vCol.a*a); }\n";

static GLuint compile(GLenum type, const char *const *src, int n) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, n, src, NULL);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        fprintf(stderr, "shader compile error:\n%s\n", log);
    }
    return s;
}
static int shader_errors;
static GLuint program(const char *vs, const char *fs, bool common) {
    const char *head = "#version 330 core\n";
    const char *vsrc[3] = {head, common ? GLSL_COMMON : "", vs};
    const char *fsrc[3] = {head, common ? GLSL_COMMON : "", fs};
    GLuint v = compile(GL_VERTEX_SHADER, vsrc, 3), f = compile(GL_FRAGMENT_SHADER, fsrc, 3);
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    GLint ok;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetProgramInfoLog(p, sizeof(log), NULL, log);
        fprintf(stderr, "program link error:\n%s\n", log);
        shader_errors++;
    }
    glDeleteShader(v);
    glDeleteShader(f);
    return p;
}
#define U(p, name) glGetUniformLocation(p, name)

/* ================================================================== meshes */
typedef struct { GLuint vbo; int count; } Mesh;
typedef struct { float *d; int n, cap; } FBuf;
static void fb_push(FBuf *b, v3 p, v3 n) {
    if (b->n + 6 > b->cap) { b->cap = b->cap ? b->cap * 2 : 4096; b->d = realloc(b->d, sizeof(float) * (size_t)b->cap); }
    float *o = b->d + b->n;
    o[0] = p.x; o[1] = p.y; o[2] = p.z; o[3] = n.x; o[4] = n.y; o[5] = n.z;
    b->n += 6;
}
static void fb_tri(FBuf *b, v3 a, v3 c, v3 d) {
    v3 n = norm(cross(sub(c, a), sub(d, a)));
    fb_push(b, a, n); fb_push(b, c, n); fb_push(b, d, n);
}
static void fb_quad(FBuf *b, v3 a, v3 c, v3 d, v3 e) { fb_tri(b, a, c, d); fb_tri(b, a, d, e); }
static Mesh mesh_upload(FBuf *b) {
    Mesh m;
    glGenBuffers(1, &m.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * (size_t)b->n, b->d, GL_STATIC_DRAW);
    m.count = b->n / 6;
    free(b->d);
    b->d = NULL; b->n = b->cap = 0;
    return m;
}
static void box_faces(FBuf *b, float x0, float y0, float z0, float x1, float y1, float z1) {
    v3 p[8];
    for (int i = 0; i < 8; i++) p[i] = V(i & 1 ? x1 : x0, i & 2 ? y1 : y0, i & 4 ? z1 : z0);
    fb_quad(b, p[4], p[5], p[7], p[6]); /* +z */
    fb_quad(b, p[1], p[0], p[2], p[3]); /* -z */
    fb_quad(b, p[5], p[1], p[3], p[7]); /* +x */
    fb_quad(b, p[0], p[4], p[6], p[2]); /* -x */
    fb_quad(b, p[2], p[6], p[7], p[3]); /* +y */
    fb_quad(b, p[0], p[1], p[5], p[4]); /* -y */
}
static Mesh mk_cube(void) { FBuf b = {0}; box_faces(&b, -0.5f, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f); return mesh_upload(&b); }
static Mesh mk_wedge(void) { /* ramp: rises from y=0 at z=+0.5 to y=1 at z=-0.5 */
    FBuf b = {0};
    v3 a = V(-0.5f, 0, 0.5f), c = V(0.5f, 0, 0.5f), d = V(0.5f, 0, -0.5f), e = V(-0.5f, 0, -0.5f);
    v3 f = V(0.5f, 1, -0.5f), g = V(-0.5f, 1, -0.5f);
    fb_quad(&b, a, c, f, g);
    fb_quad(&b, d, e, g, f);
    fb_tri(&b, c, d, f);
    fb_tri(&b, e, a, g);
    fb_quad(&b, e, d, c, a);
    return mesh_upload(&b);
}
static Mesh mk_cyl(int n, bool smooth) { /* radius 0.5, y in [0,1] */
    FBuf b = {0};
    for (int i = 0; i < n; i++) {
        float a0 = 6.2831853f * i / n, a1 = 6.2831853f * (i + 1) / n;
        v3 p0 = V(cosf(a0) * 0.5f, 0, sinf(a0) * 0.5f), p1 = V(cosf(a1) * 0.5f, 0, sinf(a1) * 0.5f);
        v3 q0 = add(p0, V(0, 1, 0)), q1 = add(p1, V(0, 1, 0));
        if (smooth) {
            v3 n0 = norm(V(p0.x, 0, p0.z)), n1 = norm(V(p1.x, 0, p1.z));
            fb_push(&b, p0, n0); fb_push(&b, q1, n1); fb_push(&b, p1, n1);
            fb_push(&b, p0, n0); fb_push(&b, q0, n0); fb_push(&b, q1, n1);
        } else fb_quad(&b, p0, q0, q1, p1);
        fb_tri(&b, V(0, 1, 0), q1, q0);
        fb_tri(&b, V(0, 0, 0), p0, p1);
    }
    return mesh_upload(&b);
}
static Mesh mk_sphere(int sl, int st) { /* radius 1 */
    FBuf b = {0};
    for (int i = 0; i < st; i++)
        for (int j = 0; j < sl; j++) {
            float t0 = 3.14159265f * i / st, t1 = 3.14159265f * (i + 1) / st;
            float p0 = 6.2831853f * j / sl, p1 = 6.2831853f * (j + 1) / sl;
            v3 a = V(sinf(t0) * cosf(p0), cosf(t0), sinf(t0) * sinf(p0)), c = V(sinf(t0) * cosf(p1), cosf(t0), sinf(t0) * sinf(p1));
            v3 d = V(sinf(t1) * cosf(p1), cosf(t1), sinf(t1) * sinf(p1)), e = V(sinf(t1) * cosf(p0), cosf(t1), sinf(t1) * sinf(p0));
            fb_push(&b, a, a); fb_push(&b, c, c); fb_push(&b, d, d);
            fb_push(&b, a, a); fb_push(&b, d, d); fb_push(&b, e, e);
        }
    return mesh_upload(&b);
}
static Mesh mk_torus(int n, int m, float R, float r) { /* ring in the xy plane (faces +z) */
    FBuf b = {0};
    for (int i = 0; i < n; i++)
        for (int j = 0; j < m; j++) {
            float u0 = 6.2831853f * i / n, u1 = 6.2831853f * (i + 1) / n, w0 = 6.2831853f * j / m, w1 = 6.2831853f * (j + 1) / m;
            v3 P[4], N[4];
            float us[4] = {u0, u1, u1, u0}, ws[4] = {w0, w0, w1, w1};
            for (int k = 0; k < 4; k++) {
                v3 c = V(cosf(us[k]) * R, sinf(us[k]) * R, 0);
                N[k] = V(cosf(us[k]) * cosf(ws[k]), sinf(us[k]) * cosf(ws[k]), sinf(ws[k]));
                P[k] = add(c, mul(N[k], r));
            }
            fb_push(&b, P[0], N[0]); fb_push(&b, P[1], N[1]); fb_push(&b, P[2], N[2]);
            fb_push(&b, P[0], N[0]); fb_push(&b, P[2], N[2]); fb_push(&b, P[3], N[3]);
        }
    return mesh_upload(&b);
}
static Mesh mk_pyramid(void) { /* base 1x1 at y=0, apex at y=1 (hip roof when scaled) */
    FBuf b = {0};
    v3 a = V(-0.5f, 0, -0.5f), c = V(0.5f, 0, -0.5f), d = V(0.5f, 0, 0.5f), e = V(-0.5f, 0, 0.5f), t = V(0, 1, 0);
    fb_tri(&b, a, t, c); fb_tri(&b, c, t, d); fb_tri(&b, d, t, e); fb_tri(&b, e, t, a);
    fb_quad(&b, a, c, d, e);
    return mesh_upload(&b);
}
static Mesh mk_gable(void) { /* gable roof: ridge along z, base 1x1 at y=0, ridge at y=1 */
    FBuf b = {0};
    v3 a = V(-0.5f, 0, -0.5f), c = V(0.5f, 0, -0.5f), d = V(0.5f, 0, 0.5f), e = V(-0.5f, 0, 0.5f), r0 = V(0, 1, -0.5f), r1 = V(0, 1, 0.5f);
    fb_quad(&b, e, a, r0, r1);
    fb_quad(&b, c, d, r1, r0);
    fb_tri(&b, a, c, r0);
    fb_tri(&b, d, e, r1);
    fb_quad(&b, a, e, d, c);
    return mesh_upload(&b);
}
static Mesh mk_cone(int n) { /* radius 0.5 at y=0, apex at y=1 */
    FBuf b = {0};
    for (int i = 0; i < n; i++) {
        float a0 = 6.2831853f * i / n, a1 = 6.2831853f * (i + 1) / n;
        v3 p0 = V(cosf(a0) * 0.5f, 0, sinf(a0) * 0.5f), p1 = V(cosf(a1) * 0.5f, 0, sinf(a1) * 0.5f);
        fb_tri(&b, p0, V(0, 1, 0), p1);
        fb_tri(&b, V(0, 0, 0), p0, p1);
    }
    return mesh_upload(&b);
}

/* ================================================================== instanced renderer */
enum { MS_CUBE, MS_WEDGE, MS_CYL, MS_CYL8, MS_SPHERE, MS_BLOB, MS_TORUS, MS_PYR, MS_GABLE, MS_CONE, MS_GROUND, MS_OCEAN, MS_ROADS,
       MS_X0, MS_XN = MS_X0 + 24, /* slots filled by models.c (vehicle and character meshes) */
       MS_N = MS_XN };
static Mesh MESH[MS_N];
/* material: base color, shader pattern, metal, roughness, emission, nightOnly (emission scaled by darkness) */
typedef struct { v3 base; int pat; float metal, rough; v3 emis; float night; } Mat;
enum { PAT_PLAIN, PAT_TOWER, PAT_MIDRISE, PAT_HOUSE, PAT_ROOF, PAT_PAINT, PAT_NEON, PAT_POND, PAT_LEAF, PAT_RIBS, PAT_GROUND, PAT_OCEAN, PAT_PLANK, PAT_ROADMESH, PAT_SIDEWALK };
#define IFL 28
typedef struct { float *d; int n, cap; } IBuf;
static IBuf SI[MS_N], DI[MS_N];
static GLuint vao_s[MS_N], vbo_s[MS_N], vao_d[MS_N], vbo_d[MS_N];
static int stat_count[MS_N];

static void ipush(IBuf *b, m4 m, const Mat *mt, float seed) {
    if ((b->n + 1) * IFL > b->cap) { b->cap = b->cap ? b->cap * 2 : IFL * 256; b->d = realloc(b->d, sizeof(float) * (size_t)b->cap); }
    float *o = b->d + b->n * IFL;
    memcpy(o, m.m, 64);
    o[16] = mt->base.x; o[17] = mt->base.y; o[18] = mt->base.z; o[19] = (float)mt->pat;
    o[20] = mt->metal; o[21] = mt->rough; o[22] = seed; o[23] = mt->night;
    o[24] = mt->emis.x; o[25] = mt->emis.y; o[26] = mt->emis.z; o[27] = 0;
    b->n++;
}
static void inst(int mesh, m4 m, const Mat *mt) { ipush(&DI[mesh], m, mt, 0.5f); }
/* static instances: "always" ones (terrain, sea, roads) plus three tiers stored in position-sorted tiles,
 * drawn only within a radius: big buildings far, trees nearer, small details near the camera */
#define NTIER 3
static IBuf ST[NTIER][MS_N];
static int static_tier;      /* -1 = always drawn */
static bool detail_mode;     /* forces tier 2 */
static void inst_s(int mesh, m4 m, const Mat *mt, float seed) {
    IBuf *b = static_tier < 0 ? &SI[mesh] : &ST[detail_mode ? 2 : static_tier][mesh];
    ipush(b, m, mt, seed);
}
#define DT_SIZE 256.0f
#define DT_X0 (-300.0f)
#define DT_Z0 (-300.0f)
#define DT_NX 33
#define DT_NZ 39
static int dt_start[NTIER][MS_N][DT_NX * DT_NZ + 1];
static GLuint vao_t[NTIER][MS_N], vbo_t[NTIER][MS_N];
static float tier_radius[NTIER] = {2700, 1150, 480};
static int dt_tile(const float *inst) {
    int tx = (int)floorf((inst[12] - DT_X0) / DT_SIZE), tz = (int)floorf((inst[14] - DT_Z0) / DT_SIZE);
    tx = tx < 0 ? 0 : tx >= DT_NX ? DT_NX - 1 : tx;
    tz = tz < 0 ? 0 : tz >= DT_NZ ? DT_NZ - 1 : tz;
    return tz * DT_NX + tx;
}
static void sort_tiles(int t, int mesh) {
    IBuf *b = &ST[t][mesh];
    static int cnt[DT_NX * DT_NZ], fill[DT_NX * DT_NZ];
    memset(cnt, 0, sizeof(cnt));
    for (int i = 0; i < b->n; i++) cnt[dt_tile(b->d + i * IFL)]++;
    int *st = dt_start[t][mesh];
    st[0] = 0;
    for (int k = 0; k < DT_NX * DT_NZ; k++) st[k + 1] = st[k] + cnt[k];
    float *out = malloc(sizeof(float) * IFL * (size_t)(b->n ? b->n : 1));
    memcpy(fill, st, sizeof(fill));
    for (int i = 0; i < b->n; i++) memcpy(out + (fill[dt_tile(b->d + i * IFL)]++) * IFL, b->d + i * IFL, sizeof(float) * IFL);
    free(b->d);
    b->d = out;
    b->cap = b->n * IFL;
}
static void setup_vao(GLuint vao, GLuint mvbo, GLuint ivbo) {
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, mvbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 24, (void *)12);
    glBindBuffer(GL_ARRAY_BUFFER, ivbo);
    for (int i = 0; i < 7; i++) {
        glEnableVertexAttribArray(2 + i);
        glVertexAttribPointer(2 + i, 4, GL_FLOAT, GL_FALSE, IFL * 4, (void *)(intptr_t)(i * 16));
        glVertexAttribDivisor(2 + i, 1);
    }
    glBindVertexArray(0);
}
static void upload_static(void) {
    for (int t = 0; t < NTIER; t++)
        for (int i = 0; i < MS_N; i++) {
            sort_tiles(t, i);
            glGenVertexArrays(1, &vao_t[t][i]);
            glGenBuffers(1, &vbo_t[t][i]);
            setup_vao(vao_t[t][i], MESH[i].vbo, vbo_t[t][i]);
            glBindBuffer(GL_ARRAY_BUFFER, vbo_t[t][i]);
            glBufferData(GL_ARRAY_BUFFER, sizeof(float) * (size_t)ST[t][i].n * IFL, ST[t][i].d, GL_STATIC_DRAW);
        }
    for (int i = 0; i < MS_N; i++) {
        glBindBuffer(GL_ARRAY_BUFFER, vbo_s[i]);
        glBufferData(GL_ARRAY_BUFFER, sizeof(float) * (size_t)SI[i].n * IFL, SI[i].d, GL_STATIC_DRAW);
        stat_count[i] = SI[i].n;
    }
}
static void upload_dynamic(void) {
    for (int i = 0; i < MS_N; i++) {
        if (!DI[i].n) continue;
        glBindBuffer(GL_ARRAY_BUFFER, vbo_d[i]);
        glBufferData(GL_ARRAY_BUFFER, sizeof(float) * (size_t)DI[i].n * IFL, DI[i].d, GL_STREAM_DRAW);
    }
}
static void draw_tier(int t, float cx, float cz, float radius) {
    int x0 = (int)floorf((cx - radius - DT_X0) / DT_SIZE), x1 = (int)floorf((cx + radius - DT_X0) / DT_SIZE);
    int z0 = (int)floorf((cz - radius - DT_Z0) / DT_SIZE), z1 = (int)floorf((cz + radius - DT_Z0) / DT_SIZE);
    x0 = x0 < 0 ? 0 : x0; z0 = z0 < 0 ? 0 : z0; x1 = x1 >= DT_NX ? DT_NX - 1 : x1; z1 = z1 >= DT_NZ ? DT_NZ - 1 : z1;
    if (x0 > x1 || z0 > z1) return;
    for (int m = 0; m < MS_N; m++) {
        if (!ST[t][m].n) continue;
        glBindVertexArray(vao_t[t][m]);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_t[t][m]);
        for (int tz = z0; tz <= z1; tz++) {
            /* only the part of the row inside the circle */
            float dz = fmaxf(0, fabsf(DT_Z0 + (tz + 0.5f) * DT_SIZE - cz) - DT_SIZE * 0.5f);
            if (dz > radius) continue;
            float half = sqrtf(radius * radius - dz * dz);
            int a0 = (int)floorf((cx - half - DT_X0) / DT_SIZE), a1 = (int)floorf((cx + half - DT_X0) / DT_SIZE);
            a0 = a0 < x0 ? x0 : a0; a1 = a1 > x1 ? x1 : a1;
            if (a0 > a1) continue;
            int a = dt_start[t][m][tz * DT_NX + a0], b = dt_start[t][m][tz * DT_NX + a1 + 1];
            if (b <= a) continue;
            for (int k = 0; k < 7; k++) glVertexAttribPointer(2 + k, 4, GL_FLOAT, GL_FALSE, IFL * 4, (void *)(intptr_t)((size_t)a * IFL * 4 + (size_t)k * 16));
            glDrawArraysInstanced(GL_TRIANGLES, 0, MESH[m].count, b - a);
        }
    }
}
static float detail_cx, detail_cz, detail_r = 1;  /* radius scale: 1 = full view, smaller for the shadow pass */
static bool skip_always;
static void draw_all_instances(void) {
    for (int t = 0; t < NTIER; t++) draw_tier(t, detail_cx, detail_cz, detail_r >= 1 ? tier_radius[t] : fminf(tier_radius[t], detail_r));
    for (int i = 0; i < MS_N; i++) {
        if (stat_count[i] && !skip_always) {
            if (i == MS_ROADS) { glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(-1.5f, -4.0f); }
            glBindVertexArray(vao_s[i]);
            glDrawArraysInstanced(GL_TRIANGLES, 0, MESH[i].count, stat_count[i]);
            if (i == MS_ROADS) glDisable(GL_POLYGON_OFFSET_FILL);
        }
        if (DI[i].n) { glBindVertexArray(vao_d[i]); glDrawArraysInstanced(GL_TRIANGLES, 0, MESH[i].count, DI[i].n); }
    }
}
static void clear_dynamic(void) { for (int i = 0; i < MS_N; i++) DI[i].n = 0; }

/* ================================================================== GL state */
static GLuint P_LIT, P_DEPTH, P_SKY, P_PART, P_DOWN, P_UP, P_COMP, P_HUD;
static GLuint fbo_shadow, tex_shadow;
static GLuint fbo_msaa, rb_msaa_col, rb_msaa_dep, fbo_hdr, tex_hdr, fbo_ldr, tex_ldr;
#define NMIP 6
static GLuint fbo_mip[NMIP], tex_mip[NMIP];
static int mip_w[NMIP], mip_h[NMIP];
static int rw, rh, msaa_samples = 4;
static GLuint vao_empty, vao_part, vbo_part, vao_hud, vbo_hud, tex_font;
static const int SHADOW_RES = 2048;

static GLuint make_tex(int w, int h, GLint ifmt, GLenum fmt, GLenum type, GLint filter) {
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, ifmt, w, h, 0, fmt, type, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}
static GLuint make_fbo(GLuint tex) {
    GLuint f;
    glGenFramebuffers(1, &f);
    glBindFramebuffer(GL_FRAMEBUFFER, f);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    return f;
}
static void free_targets(void) {
    if (!fbo_msaa) return;
    glDeleteFramebuffers(1, &fbo_msaa);
    glDeleteRenderbuffers(1, &rb_msaa_col);
    glDeleteRenderbuffers(1, &rb_msaa_dep);
    glDeleteFramebuffers(1, &fbo_hdr);
    glDeleteTextures(1, &tex_hdr);
    glDeleteFramebuffers(1, &fbo_ldr);
    glDeleteTextures(1, &tex_ldr);
    glDeleteFramebuffers(NMIP, fbo_mip);
    glDeleteTextures(NMIP, tex_mip);
}
static void make_targets(int w, int h) {
    free_targets();
    rw = w;
    rh = h;
    glGenFramebuffers(1, &fbo_msaa);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_msaa);
    glGenRenderbuffers(1, &rb_msaa_col);
    glBindRenderbuffer(GL_RENDERBUFFER, rb_msaa_col);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, msaa_samples, GL_RGBA16F, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb_msaa_col);
    glGenRenderbuffers(1, &rb_msaa_dep);
    glBindRenderbuffer(GL_RENDERBUFFER, rb_msaa_dep);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, msaa_samples, GL_DEPTH_COMPONENT24, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rb_msaa_dep);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) fprintf(stderr, "msaa fbo incomplete\n");
    tex_hdr = make_tex(w, h, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR);
    fbo_hdr = make_fbo(tex_hdr);
    tex_ldr = make_tex(w, h, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR);
    fbo_ldr = make_fbo(tex_ldr);
    int mw = w, mh = h;
    for (int i = 0; i < NMIP; i++) {
        mw = mw / 2 > 1 ? mw / 2 : 1;
        mh = mh / 2 > 1 ? mh / 2 : 1;
        mip_w[i] = mw;
        mip_h[i] = mh;
        tex_mip[i] = make_tex(mw, mh, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR);
        fbo_mip[i] = make_fbo(tex_mip[i]);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

/* 5x7 pixel font atlas */
static const char FONT_CHARS[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ:.-!/?, +<>'()=*%$#";
static const unsigned char FONT[][7] = {
    {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},{31,2,4,2,1,17,14},{2,6,10,18,31,2,2},
    {31,16,30,1,1,17,14},{6,8,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},{14,17,17,15,1,2,12},
    {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},{28,18,17,17,17,18,28},
    {31,16,16,30,16,16,31},{31,16,16,30,16,16,16},{14,17,16,23,17,17,15},{17,17,17,31,17,17,17},
    {14,4,4,4,4,4,14},{7,2,2,2,2,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17},{17,17,25,21,19,17,17},{14,17,17,17,17,17,14},{30,17,17,30,16,16,16},
    {14,17,17,17,21,18,13},{30,17,17,30,20,18,17},{15,16,16,14,1,1,30},{31,4,4,4,4,4,4},
    {17,17,17,17,17,17,14},{17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
    {17,17,17,10,4,4,4},{31,1,2,4,8,16,31},
    {0,12,12,0,12,12,0},{0,0,0,0,0,12,12},{0,0,0,31,0,0,0},{4,4,4,4,4,0,4},{1,1,2,4,8,16,16},
    {14,17,1,2,4,0,4},{0,0,0,0,12,4,8},{0,0,0,0,0,0,0},{0,4,4,31,4,4,0},{2,4,8,16,8,4,2},{8,4,2,1,2,4,8},
    {4,4,8,0,0,0,0},{2,4,8,8,8,4,2},{8,4,2,2,2,4,8},{0,0,31,0,31,0,0},{0,21,14,31,14,21,0},{24,25,2,4,8,19,3},
    {4,15,20,14,5,30,4},{10,31,10,10,31,10,0},
};
#define NGLYPH ((int)sizeof(FONT) / 7)
static void make_font(void) {
    int w = NGLYPH * 6, h = 8;
    unsigned char *px = calloc((size_t)(w * h), 1);
    for (int g = 0; g < NGLYPH; g++)
        for (int y = 0; y < 7; y++)
            for (int x = 0; x < 5; x++)
                if (FONT[g][y] & (16 >> x)) px[y * w + g * 6 + x] = 255;
    glGenTextures(1, &tex_font);
    glBindTexture(GL_TEXTURE_2D, tex_font);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    free(px);
}

static Mesh mk_ground(void);
static Mesh mk_ocean(void);
static Mesh mk_roads(void);
static void models_init(void);
static void init_gl(void) {
    P_LIT = program(VS_LIT, FS_LIT, true);
    P_DEPTH = program(VS_DEPTH, FS_DEPTH, false);
    P_SKY = program(VS_FULL, FS_SKY, true);
    P_PART = program(VS_PART, FS_PART, false);
    P_DOWN = program(VS_FULL, FS_DOWN, false);
    P_UP = program(VS_FULL, FS_UP, false);
    P_COMP = program(VS_FULL, FS_COMP, true);
    P_HUD = program(VS_HUD, FS_HUD, false);
    MESH[MS_CUBE] = mk_cube();
    MESH[MS_WEDGE] = mk_wedge();
    MESH[MS_CYL] = mk_cyl(18, true);
    MESH[MS_CYL8] = mk_cyl(8, false);
    MESH[MS_SPHERE] = mk_sphere(18, 12);
    MESH[MS_BLOB] = mk_sphere(7, 5);
    MESH[MS_TORUS] = mk_torus(40, 8, 1, 0.08f);
    MESH[MS_PYR] = mk_pyramid();
    MESH[MS_GABLE] = mk_gable();
    MESH[MS_CONE] = mk_cone(9);
    /* ground, sea and roads are built after the world exists (finish_world_meshes) */
    models_init();
    for (int i = 0; i < MS_N; i++) {
        glGenVertexArrays(1, &vao_s[i]);
        glGenVertexArrays(1, &vao_d[i]);
        glGenBuffers(1, &vbo_s[i]);
        glGenBuffers(1, &vbo_d[i]);
        setup_vao(vao_s[i], MESH[i].vbo, vbo_s[i]);
        setup_vao(vao_d[i], MESH[i].vbo, vbo_d[i]);
    }
    glGenVertexArrays(1, &vao_empty);
    glGenTextures(1, &tex_shadow);
    glBindTexture(GL_TEXTURE_2D, tex_shadow);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, SHADOW_RES, SHADOW_RES, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glGenFramebuffers(1, &fbo_shadow);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_shadow);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, tex_shadow, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glGenVertexArrays(1, &vao_part);
    glGenBuffers(1, &vbo_part);
    glBindVertexArray(vao_part);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_part);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 40, (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 40, (void *)12);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 40, (void *)24);
    glGenVertexArrays(1, &vao_hud);
    glGenBuffers(1, &vbo_hud);
    glBindVertexArray(vao_hud);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_hud);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 32, (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 32, (void *)8);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 32, (void *)16);
    make_font();
}

static void finish_world_meshes(void) {
    MESH[MS_GROUND] = mk_ground();
    MESH[MS_OCEAN] = mk_ocean();
    MESH[MS_ROADS] = mk_roads();
    int ms[3] = {MS_GROUND, MS_OCEAN, MS_ROADS};
    for (int k = 0; k < 3; k++) { setup_vao(vao_s[ms[k]], MESH[ms[k]].vbo, vbo_s[ms[k]]); setup_vao(vao_d[ms[k]], MESH[ms[k]].vbo, vbo_d[ms[k]]); }
}

/* ================================================================== camera + particles */
typedef struct { m4 view, proj, vp; v3 pos, right, up, fwd; float fov; } Cam;
static Cam cam;
static void setup_camera(v3 eye, v3 at, float fov_deg) {
    v3 fwd = norm(sub(at, eye));
    cam.pos = eye;
    cam.fwd = fwd;
    cam.fov = fov_deg;
    cam.view = m4_look(eye, at, V(0, 1, 0));
    cam.proj = m4_persp(fov_deg * 3.14159f / 180, (float)rw / rh, 0.4f, 7000);
    cam.vp = m4_mul(cam.proj, cam.view);
    cam.right = norm(cross(fwd, V(0, 1, 0)));
    cam.up = cross(cam.right, fwd);
}

static float *pbuf;
static int pcount, pcap;
static void pquad(v3 a, v3 b, v3 c, v3 d, v3 col, float alpha, float kind) {
    if ((pcount + 6) * 10 > pcap) { pcap = pcap ? pcap * 2 : 60000; pbuf = realloc(pbuf, sizeof(float) * (size_t)pcap); }
    v3 P[6] = {a, b, c, a, c, d};
    float UV[6][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 0}, {1, 1}, {0, 1}};
    for (int i = 0; i < 6; i++) {
        float *o = pbuf + (pcount + i) * 10;
        o[0] = P[i].x; o[1] = P[i].y; o[2] = P[i].z;
        o[3] = UV[i][0]; o[4] = UV[i][1]; o[5] = kind;
        o[6] = col.x; o[7] = col.y; o[8] = col.z; o[9] = alpha;
    }
    pcount += 6;
}
static float near_fade(v3 p, float size) {
    float d = len(sub(p, cam.pos));
    return clampf((d - size * 0.8f - 0.3f) / (size + 1.0f), 0, 1);
}
static void billboard(v3 p, float s, v3 col, float alpha, float kind) {
    alpha *= near_fade(p, s);
    if (alpha <= 0.001f) return;
    v3 r = mul(cam.right, s), u = mul(cam.up, s);
    pquad(sub(sub(p, r), u), sub(add(p, r), u), add(add(p, r), u), add(sub(p, r), u), col, alpha, kind);
}
static void beam(v3 a, v3 b, float w, v3 col, float alpha) {
    alpha *= fminf(near_fade(a, 0.3f), near_fade(b, 0.3f));
    if (alpha <= 0.001f) return;
    v3 d = sub(b, a);
    v3 side = mul(norm(cross(d, sub(a, cam.pos))), w);
    pquad(sub(a, side), sub(b, side), add(b, side), add(a, side), col, alpha, 1);
}
static void ground_ring(v3 c, float r, v3 col, float alpha) {
    pquad(add(c, V(-r, 0, -r)), add(c, V(r, 0, -r)), add(c, V(r, 0, r)), add(c, V(-r, 0, r)), col, alpha, 2);
}
static void flush_particles(m4 vp) {
    if (!pcount) return;
    glUseProgram(P_PART);
    glUniformMatrix4fv(U(P_PART, "uVP"), 1, GL_FALSE, vp.m);
    glBindVertexArray(vao_part);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_part);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * (size_t)pcount * 10, pbuf, GL_STREAM_DRAW);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDrawArrays(GL_TRIANGLES, 0, pcount);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    pcount = 0;
}

/* ================================================================== HUD primitives */
static float *hbuf;
static int hcount, hcap;
static void hgrow(int n) {
    if ((hcount + n) * 8 > hcap) { hcap = hcap ? hcap * 2 : 40000; if ((hcount + n) * 8 > hcap) hcap = (hcount + n) * 16; hbuf = realloc(hbuf, sizeof(float) * (size_t)hcap); }
}
static void hvert(float x, float y, float u, float v, float r, float g, float b, float a) {
    float *o = hbuf + hcount * 8;
    o[0] = x; o[1] = y; o[2] = u; o[3] = v; o[4] = r; o[5] = g; o[6] = b; o[7] = a;
    hcount++;
}
static void hquad(float x, float y, float w, float h, float u0, float v0, float u1, float v1, float r, float g, float b, float a) {
    hgrow(6);
    hvert(x, y, u0, v0, r, g, b, a); hvert(x + w, y, u1, v0, r, g, b, a); hvert(x + w, y + h, u1, v1, r, g, b, a);
    hvert(x, y, u0, v0, r, g, b, a); hvert(x + w, y + h, u1, v1, r, g, b, a); hvert(x, y + h, u0, v1, r, g, b, a);
}
static void hrect(float x, float y, float w, float h, float r, float g, float b, float a) { hquad(x, y, w, h, -1, -1, -1, -1, r, g, b, a); }
static void htri(float x0, float y0, float x1, float y1, float x2, float y2, float r, float g, float b, float a) {
    hgrow(3);
    hvert(x0, y0, -1, -1, r, g, b, a); hvert(x1, y1, -1, -1, r, g, b, a); hvert(x2, y2, -1, -1, r, g, b, a);
}
static void hline(float x0, float y0, float x1, float y1, float w, float r, float g, float b, float a) {
    float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy);
    if (l < 1e-3f) return;
    float nx = -dy / l * w * 0.5f, ny = dx / l * w * 0.5f;
    htri(x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, r, g, b, a);
    htri(x0 + nx, y0 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny, r, g, b, a);
}
static void hstar(float cx, float cy, float R, float r, float g, float b, float a) {
    for (int i = 0; i < 5; i++) {
        float a0 = -1.5708f + i * 1.2566f, a1 = a0 + 0.6283f, a2 = a0 - 0.6283f;
        float ri = R * 0.42f;
        htri(cx, cy, cx + cosf(a0) * R, cy + sinf(a0) * R, cx + cosf(a1) * ri, cy + sinf(a1) * ri, r, g, b, a);
        htri(cx, cy, cx + cosf(a2) * ri, cy + sinf(a2) * ri, cx + cosf(a0) * R, cy + sinf(a0) * R, r, g, b, a);
    }
}
static float text_w(const char *s, float sc) { return (float)strlen(s) * 6 * sc - sc; }
static void htext(const char *s, float x, float y, float sc, float r, float g, float b, float a, int align) {
    if (align == 1) x -= text_w(s, sc) / 2;
    if (align == 2) x -= text_w(s, sc);
    float aw = (float)(NGLYPH * 6);
    for (; *s; s++, x += 6 * sc) {
        char c = *s >= 'a' && *s <= 'z' ? (char)(*s - 32) : *s;
        const char *at = strchr(FONT_CHARS, c);
        if (!at || c == ' ' || c == 0) continue;
        int gi = (int)(at - FONT_CHARS);
        float u0 = gi * 6 / aw, u1 = (gi * 6 + 5) / aw;
        hquad(x + sc * 0.6f, y + sc * 0.6f, 5 * sc, 7 * sc, u0, 0, u1, 7 / 8.0f, 0, 0, 0, a * 0.6f);
        hquad(x, y, 5 * sc, 7 * sc, u0, 0, u1, 7 / 8.0f, r, g, b, a);
    }
}
static GLuint hud_tex_override;
static void hud_flush(void) {
    if (!hcount) return;
    glUseProgram(P_HUD);
    glUniform2f(U(P_HUD, "uScreen"), (float)rw, (float)rh);
    glUniform1i(U(P_HUD, "uFont"), 0);
    glUniform1i(U(P_HUD, "uMode"), hud_tex_override ? 1 : 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, hud_tex_override ? hud_tex_override : tex_font);
    glBindVertexArray(vao_hud);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_hud);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * (size_t)hcount * 8, hbuf, GL_STREAM_DRAW);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLES, 0, hcount);
    glDisable(GL_BLEND);
    hcount = 0;
}
