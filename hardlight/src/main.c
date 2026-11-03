/*
 * HARDLIGHT - a compact arena shooter with a modern renderer.
 * Native Linux, C11 + SDL2 + OpenGL 3.3 core. No asset files: geometry, materials,
 * sky, sound and music are all generated at startup.
 *
 * Renderer: physically based shading (GGX), shadow-mapped moonlight, up to 32 dynamic
 * point lights, procedural sky + reflections, 4x MSAA HDR target, bloom chain,
 * ACES tonemapping, chromatic aberration / vignette / grain, additive particles.
 *
 *   ./hardlight                  play
 *   ./hardlight --selftest DIR   headless: compile shaders, let a bot fight waves,
 *                                time the GPU, write screenshots to DIR
 */
#define GL_GLEXT_PROTOTYPES 1
#include <SDL.h>
#include <GL/glcorearb.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
static v3 norm(v3 a) { float l = len(a); return l > 1e-8f ? mul(a, 1 / l) : V(0, 1, 0); }
static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static float frand(void) { return (float)rand() / (float)RAND_MAX; }
static float frs(void) { return frand() * 2 - 1; }

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
/* T * Ry(yaw) * Rx(pitch) * Rz(roll) * S */
static m4 m4_trs(v3 t, float yaw, float pitch, float roll, v3 s) {
    float cy = cosf(yaw), sy = sinf(yaw), cp = cosf(pitch), sp = sinf(pitch), cr = cosf(roll), sr = sinf(roll);
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
    "uniform vec3 uSunDir;\n"                                                                          \
    "vec3 skyBase(vec3 d){\n"                                                                          \
    " float h=d.y; vec3 zen=vec3(0.006,0.008,0.030), hor=vec3(0.15,0.05,0.11), low=vec3(0.02,0.012,0.025);\n" \
    " vec3 c=mix(hor,zen,pow(clamp(h,0.0,1.0),0.42));\n"                                               \
    " if(h<0.0) c=mix(hor*0.8,low,clamp(-h*4.0,0.0,1.0));\n"                                           \
    " float s=max(dot(d,uSunDir),0.0);\n"                                                              \
    " c+=vec3(1.0,0.38,0.22)*pow(s,6.0)*0.55*(1.0-clamp(abs(h)*1.6,0.0,1.0));\n"                     \
    " c+=vec3(0.9,0.6,0.7)*pow(s,120.0)*1.2;\n"                                                         \
    " return c;}\n"                                                                                    \
    "vec3 skyFull(vec3 d){\n"                                                                          \
    " vec3 c=skyBase(d); float h=d.y;\n"                                                              \
    " float s=dot(d,uSunDir); c+=vec3(1.0,0.86,0.78)*smoothstep(0.9990,0.9994,s)*9.0;\n"               \
    " float n=vnoise(d*4.0)*0.6+vnoise(d*9.0)*0.3+vnoise(d*21.0)*0.1;\n"                              \
    " float band=exp(-pow((d.y-0.35+d.x*0.25)*3.0,2.0));\n"                                            \
    " c+=vec3(0.30,0.08,0.42)*pow(n,3.0)*band*0.75*clamp(h*3.0,0.0,1.0);\n"                           \
    " c+=vec3(0.05,0.20,0.35)*pow(vnoise(d*6.0+7.0),4.0)*band*clamp(h*3.0,0.0,1.0);\n"               \
    " vec3 p=d*320.0; vec3 id=floor(p); float r=hash13(id);\n"                                        \
    " if(r>0.9965&&h>0.02){float k=smoothstep(0.45,0.0,length(fract(p)-0.5)); c+=vec3(0.9,0.95,1.0)*k*(r-0.9965)*900.0*clamp(h*4.0,0.0,1.0);}\n" \
    " return c;}\n"

static const char *VS_LIT =
    "layout(location=0) in vec3 aPos; layout(location=1) in vec3 aNrm;\n"
    "uniform mat4 uModel, uVP, uLightVP;\n"
    "out vec3 vPos; out vec3 vNrm; out vec4 vSh;\n"
    "void main(){ vec4 w=uModel*vec4(aPos,1.0); vPos=w.xyz; vNrm=mat3(uModel)*aNrm; vSh=uLightVP*w; gl_Position=uVP*w; }\n";

static const char *FS_LIT =
    "in vec3 vPos; in vec3 vNrm; in vec4 vSh; out vec4 frag;\n"
    "uniform vec3 uCam, uBase, uEmis, uTrim, uSunCol, uFogCol; uniform float uMetal, uRough, uTime, uFogDen, uFlash;\n"
    "uniform int uPattern, uNumLights; uniform vec4 uLightPos[32]; uniform vec3 uLightCol[32];\n"
    "uniform sampler2DShadow uShadow;\n"
    "const float PI=3.14159265;\n"
    "float D_GGX(float nh,float a){float a2=a*a; float d=nh*nh*(a2-1.0)+1.0; return a2/(PI*d*d);}\n"
    "float G_S(float nv,float nl,float r){float k=(r+1.0)*(r+1.0)/8.0; return (nv/(nv*(1.0-k)+k))*(nl/(nl*(1.0-k)+k));}\n"
    "vec3 F_S(float c,vec3 f0){return f0+(1.0-f0)*pow(1.0-c,5.0);}\n"
    "vec3 shade(vec3 N,vec3 V,vec3 L,vec3 alb,float met,float rough,vec3 rad){\n"
    " vec3 H=normalize(V+L); float nl=max(dot(N,L),0.0), nv=max(dot(N,V),1e-3), nh=max(dot(N,H),0.0);\n"
    " vec3 f0=mix(vec3(0.04),alb,met); vec3 F=F_S(max(dot(H,V),0.0),f0);\n"
    " vec3 spec=D_GGX(nh,rough*rough)*G_S(nv,nl,rough)*F/(4.0*nv*max(nl,1e-3)+1e-4);\n"
    " vec3 kd=(1.0-F)*(1.0-met); return (kd*alb/PI+spec)*rad*nl;}\n"
    "float shadowAt(vec3 N){ vec3 p=vSh.xyz/vSh.w*0.5+0.5; if(p.z>1.0||p.x<0.0||p.x>1.0||p.y<0.0||p.y>1.0) return 1.0;\n"
    " float b=0.0012+0.002*(1.0-max(dot(N,uSunDir),0.0)); float s=0.0; vec2 t=1.0/vec2(textureSize(uShadow,0));\n"
    " for(int y=-1;y<=1;y++) for(int x=-1;x<=1;x++) s+=texture(uShadow,vec3(p.xy+vec2(x,y)*t*1.2,p.z-b));\n"
    " return s/9.0;}\n"
    "vec2 planar(vec3 N){ vec3 a=abs(N); return a.y>0.6?vPos.xz:(a.x>a.z?vPos.zy:vPos.xy); }\n"
    "void main(){\n"
    " vec3 N=normalize(vNrm); vec3 V=normalize(uCam-vPos);\n"
    " vec3 alb=uBase; float rough=uRough, met=uMetal; vec3 emis=uEmis;\n"
    " if(uPattern==1){ vec2 g=vPos.xz/2.0; vec2 f=abs(fract(g)-0.5); float seam=smoothstep(0.475,0.492,max(f.x,f.y));\n"
    "   float pv=hash12(floor(g)); alb*=mix(0.8+0.35*pv,0.25,seam); rough=clamp(mix(rough*(0.6+0.8*pv),0.95,seam),0.05,1.0);\n"
    "   vec2 b=abs(fract(vPos.xz/12.0+0.5)-0.5); float line=smoothstep(0.494,0.4985,max(b.x,b.y));\n"
    "   float pulse=0.6+0.4*sin(uTime*1.5-length(vPos.xz)*0.25);\n"
    "   emis+=uTrim*line*pulse*2.0; alb*=0.9+0.2*vnoise(vPos*3.1); }\n"
    " else if(uPattern==2){ vec2 g=planar(N)/vec2(1.6,1.2); vec2 f=abs(fract(g)-0.5); float seam=smoothstep(0.47,0.49,max(f.x,f.y));\n"
    "   float pv=hash12(floor(g)); alb*=mix(0.85+0.3*pv,0.3,seam); rough=mix(rough*(0.7+0.6*pv),0.9,seam);\n"
    "   float band=smoothstep(0.06,0.0,abs(fract(vPos.y/3.0)-0.15))*step(0.5,1.0-abs(N.y));\n"
    "   emis+=uTrim*band*2.5; }\n"
    " else if(uPattern==3){ emis*=0.75+0.25*sin(uTime*6.0+vPos.y*4.0); }\n"
    " vec3 col=vec3(0.0);\n"
    " float sh=shadowAt(N); col+=shade(N,V,uSunDir,alb,met,rough,uSunCol)*sh;\n"
    " for(int i=0;i<uNumLights;i++){ vec3 d=uLightPos[i].xyz-vPos; float dist=length(d); float r=uLightPos[i].w;\n"
    "   float fall=clamp(1.0-pow(dist/r,4.0),0.0,1.0); fall=fall*fall/(dist*dist+1.0);\n"
    "   if(fall>0.0) col+=shade(N,V,d/dist,alb,met,rough,uLightCol[i]*fall); }\n"
    " vec3 amb=mix(vec3(0.020,0.014,0.024),vec3(0.035,0.040,0.085),N.y*0.5+0.5);\n"
    " vec3 f0=mix(vec3(0.04),alb,met); float nv=max(dot(N,V),0.0);\n"
    " vec3 F=f0+(max(vec3(1.0-rough),f0)-f0)*pow(1.0-nv,5.0);\n"
    " vec3 R=reflect(-V,N); vec3 env=skyBase(R)*mix(0.7,0.1,rough);\n"
    " col+=amb*alb*(1.0-met)*(0.6+0.4*sh)+env*F*(1.0-rough*0.7);\n"
    " col+=emis+vec3(uFlash);\n"
    " float dist=length(uCam-vPos); float fog=1.0-exp(-pow(dist*uFogDen,2.0));\n"
    " fog*=clamp(exp(-max(vPos.y,0.0)*0.04),0.0,1.0);\n"
    " col=mix(col,uFogCol+skyBase(-V)*0.25,fog);\n"
    " frag=vec4(col,1.0);}\n";

static const char *VS_DEPTH =
    "layout(location=0) in vec3 aPos; uniform mat4 uModel, uLightVP;\n"
    "void main(){ gl_Position=uLightVP*uModel*vec4(aPos,1.0); }\n";
static const char *FS_DEPTH = "out vec4 frag; void main(){ frag=vec4(1.0); }\n";

static const char *VS_FULL =
    "out vec2 vUV; void main(){ vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2); vUV=p; gl_Position=vec4(p*2.0-1.0,0.0,1.0); }\n";

static const char *FS_SKY =
    "in vec2 vUV; out vec4 frag; uniform mat4 uInvVP;\n"
    "void main(){ vec4 a=uInvVP*vec4(vUV*2.0-1.0,-1.0,1.0); vec4 b=uInvVP*vec4(vUV*2.0-1.0,1.0,1.0);\n"
    " vec3 d=normalize(b.xyz/b.w-a.xyz/a.w); frag=vec4(skyFull(d),1.0); }\n";

static const char *VS_PART =
    "layout(location=0) in vec3 aPos; layout(location=1) in vec3 aUV; layout(location=2) in vec4 aCol;\n"
    "uniform mat4 uVP; out vec3 vUV; out vec4 vCol;\n"
    "void main(){ gl_Position=uVP*vec4(aPos,1.0); vUV=aUV; vCol=aCol; }\n";
static const char *FS_PART =
    "in vec3 vUV; in vec4 vCol; out vec4 frag;\n"
    "void main(){ vec2 p=vUV.xy*2.0-1.0; float a;\n"
    " if(vUV.z<0.5){ a=max(0.0,1.0-dot(p,p)); a*=a; }\n"
    " else if(vUV.z<1.5){ a=max(0.0,1.0-abs(p.y)); a=a*a*a; }\n"
    " else { float r=length(p); a=smoothstep(1.0,0.82,r)*smoothstep(0.55,0.85,r); }\n"
    " frag=vec4(vCol.rgb*vCol.a*a,0.0); }\n";

static const char *FS_DOWN =
    "in vec2 vUV; out vec4 frag; uniform sampler2D uSrc; uniform vec2 uTexel; uniform int uFirst;\n"
    "void main(){ vec3 c=texture(uSrc,vUV+uTexel*vec2(-1,-1)).rgb+texture(uSrc,vUV+uTexel*vec2(1,-1)).rgb\n"
    " +texture(uSrc,vUV+uTexel*vec2(-1,1)).rgb+texture(uSrc,vUV+uTexel*vec2(1,1)).rgb; c*=0.25;\n"
    " if(uFirst==1){ float b=max(c.r,max(c.g,c.b)); float k=max(b-1.3,0.0);\n"
    "   c*=k/max(b,1e-4); c=min(c,vec3(40.0)); }\n"
    " frag=vec4(c,1.0); }\n";
static const char *FS_UP =
    "in vec2 vUV; out vec4 frag; uniform sampler2D uSrc; uniform vec2 uTexel;\n"
    "void main(){ vec3 c=texture(uSrc,vUV).rgb*4.0;\n"
    " c+=(texture(uSrc,vUV+uTexel*vec2(-1,0)).rgb+texture(uSrc,vUV+uTexel*vec2(1,0)).rgb+texture(uSrc,vUV+uTexel*vec2(0,-1)).rgb+texture(uSrc,vUV+uTexel*vec2(0,1)).rgb)*2.0;\n"
    " c+=texture(uSrc,vUV+uTexel*vec2(-1,-1)).rgb+texture(uSrc,vUV+uTexel*vec2(1,-1)).rgb+texture(uSrc,vUV+uTexel*vec2(-1,1)).rgb+texture(uSrc,vUV+uTexel*vec2(1,1)).rgb;\n"
    " frag=vec4(c/16.0,1.0); }\n";
static const char *FS_COMP =
    "in vec2 vUV; out vec4 frag; uniform sampler2D uHdr, uBloom; uniform float uExposure, uDamage, uTime, uLowHp, uBloomAmt;\n"
    "vec3 aces(vec3 x){ return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14),0.0,1.0); }\n"
    "void main(){ vec2 cc=vUV-0.5; float ca=0.0012+uDamage*0.012;\n"
    " vec3 c=vec3(texture(uHdr,vUV+cc*ca).r,texture(uHdr,vUV).g,texture(uHdr,vUV-cc*ca).b);\n"
    " c+=texture(uBloom,vUV).rgb*uBloomAmt; c*=uExposure; c=aces(c);\n"
    " float r=length(cc*vec2(1.0,0.75)); float vig=smoothstep(0.85,0.25,r); c*=mix(0.45,1.0,vig);\n"
    " c=mix(c,c*vec3(1.0,0.3,0.25)+vec3(0.35,0.0,0.02)*(1.0-vig),clamp(uDamage,0.0,1.0)*0.7);\n"
    " float g=dot(c,vec3(0.3,0.59,0.11)); c=mix(c,vec3(g)*vec3(1.05,0.95,0.95),uLowHp*0.55);\n"
    " c=pow(c,vec3(1.0/2.2)); c+=(hash12(gl_FragCoord.xy+fract(uTime)*100.0)-0.5)*0.03;\n"
    " frag=vec4(c,1.0); }\n";

static const char *VS_HUD =
    "layout(location=0) in vec2 aPos; layout(location=1) in vec2 aUV; layout(location=2) in vec4 aCol;\n"
    "uniform vec2 uScreen; out vec2 vUV; out vec4 vCol;\n"
    "void main(){ gl_Position=vec4(aPos/uScreen*vec2(2.0,-2.0)+vec2(-1.0,1.0),0.0,1.0); vUV=aUV; vCol=aCol; }\n";
static const char *FS_HUD =
    "in vec2 vUV; in vec4 vCol; out vec4 frag; uniform sampler2D uFont;\n"
    "void main(){ float a=vUV.x<0.0?1.0:texture(uFont,vUV).r; frag=vec4(vCol.rgb,vCol.a*a); }\n";

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
typedef struct { GLuint vao, vbo; int count; } Mesh;
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
static Mesh mesh_upload(FBuf *b) {
    Mesh m = {0};
    glGenVertexArrays(1, &m.vao);
    glGenBuffers(1, &m.vbo);
    glBindVertexArray(m.vao);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * (size_t)b->n, b->d, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 24, (void *)12);
    m.count = b->n / 6;
    free(b->d);
    *b = (FBuf){0};
    return m;
}
static Mesh mk_cube(void) { /* unit cube centred on the origin */
    FBuf b = {0};
    v3 p[8];
    for (int i = 0; i < 8; i++) p[i] = V(i & 1 ? 0.5f : -0.5f, i & 2 ? 0.5f : -0.5f, i & 4 ? 0.5f : -0.5f);
    int f[6][4] = {{1, 3, 7, 5}, {0, 4, 6, 2}, {2, 6, 7, 3}, {0, 1, 5, 4}, {4, 5, 7, 6}, {0, 2, 3, 1}};
    for (int i = 0; i < 6; i++) {
        fb_tri(&b, p[f[i][0]], p[f[i][1]], p[f[i][2]]);
        fb_tri(&b, p[f[i][0]], p[f[i][2]], p[f[i][3]]);
    }
    return mesh_upload(&b);
}
static Mesh mk_wedge(void) { /* ramp: height 1 at z=-0.5 down to 0 at z=+0.5, x in [-0.5,0.5] */
    FBuf b = {0};
    v3 a = V(-0.5f, 0, -0.5f), c = V(0.5f, 0, -0.5f), d = V(0.5f, 0, 0.5f), e = V(-0.5f, 0, 0.5f);
    v3 at = V(-0.5f, 1, -0.5f), ct = V(0.5f, 1, -0.5f);
    fb_tri(&b, at, e, d); fb_tri(&b, at, d, ct);      /* slope */
    fb_tri(&b, a, at, ct); fb_tri(&b, a, ct, c);      /* back */
    fb_tri(&b, a, e, at); fb_tri(&b, c, ct, d);       /* sides */
    return mesh_upload(&b);
}
static Mesh mk_cyl(int n, bool smooth) { /* radius 0.5, y in [0,1] */
    FBuf b = {0};
    for (int i = 0; i < n; i++) {
        float a0 = i * 6.2832f / n, a1 = (i + 1) * 6.2832f / n;
        v3 p0 = V(cosf(a0) * 0.5f, 0, sinf(a0) * 0.5f), p1 = V(cosf(a1) * 0.5f, 0, sinf(a1) * 0.5f);
        v3 q0 = add(p0, V(0, 1, 0)), q1 = add(p1, V(0, 1, 0));
        if (smooth) {
            v3 n0 = V(cosf(a0), 0, sinf(a0)), n1 = V(cosf(a1), 0, sinf(a1));
            fb_push(&b, p0, n0); fb_push(&b, q1, n1); fb_push(&b, p1, n1);
            fb_push(&b, p0, n0); fb_push(&b, q0, n0); fb_push(&b, q1, n1);
        } else {
            fb_tri(&b, p0, q1, p1);
            fb_tri(&b, p0, q0, q1);
        }
        fb_tri(&b, V(0, 1, 0), q1, q0);
        fb_tri(&b, V(0, 0, 0), p0, p1);
    }
    return mesh_upload(&b);
}
static Mesh mk_sphere(int sl, int st) { /* radius 1 */
    FBuf b = {0};
    for (int j = 0; j < st; j++)
        for (int i = 0; i < sl; i++) {
            float t0 = j * 3.14159f / st, t1 = (j + 1) * 3.14159f / st;
            float p0 = i * 6.2832f / sl, p1 = (i + 1) * 6.2832f / sl;
            v3 a = V(sinf(t0) * cosf(p0), cosf(t0), sinf(t0) * sinf(p0));
            v3 c = V(sinf(t0) * cosf(p1), cosf(t0), sinf(t0) * sinf(p1));
            v3 d = V(sinf(t1) * cosf(p1), cosf(t1), sinf(t1) * sinf(p1));
            v3 e = V(sinf(t1) * cosf(p0), cosf(t1), sinf(t1) * sinf(p0));
            fb_push(&b, a, a); fb_push(&b, c, c); fb_push(&b, d, d);
            fb_push(&b, a, a); fb_push(&b, d, d); fb_push(&b, e, e);
        }
    return mesh_upload(&b);
}
static Mesh mk_torus(int n, int m, float R, float r) {
    FBuf b = {0};
    for (int i = 0; i < n; i++)
        for (int j = 0; j < m; j++) {
            float u[2] = {i * 6.2832f / n, (i + 1) * 6.2832f / n}, w[2] = {j * 6.2832f / m, (j + 1) * 6.2832f / m};
            v3 P[2][2], N[2][2];
            for (int a = 0; a < 2; a++)
                for (int c = 0; c < 2; c++) {
                    v3 cen = V(cosf(u[a]) * R, 0, sinf(u[a]) * R);
                    N[a][c] = V(cosf(u[a]) * cosf(w[c]), sinf(w[c]), sinf(u[a]) * cosf(w[c]));
                    P[a][c] = add(cen, mul(N[a][c], r));
                }
            fb_push(&b, P[0][0], N[0][0]); fb_push(&b, P[1][1], N[1][1]); fb_push(&b, P[1][0], N[1][0]);
            fb_push(&b, P[0][0], N[0][0]); fb_push(&b, P[0][1], N[0][1]); fb_push(&b, P[1][1], N[1][1]);
        }
    return mesh_upload(&b);
}
static Mesh mk_spike(void) { /* a 4-sided pyramid pointing +y, base at y=0 */
    FBuf b = {0};
    v3 t = V(0, 1, 0), p[4] = {V(-0.5f, 0, -0.5f), V(0.5f, 0, -0.5f), V(0.5f, 0, 0.5f), V(-0.5f, 0, 0.5f)};
    for (int i = 0; i < 4; i++) fb_tri(&b, p[i], t, p[(i + 1) % 4]);
    fb_tri(&b, p[0], p[1], p[2]);
    fb_tri(&b, p[0], p[2], p[3]);
    return mesh_upload(&b);
}

enum { MS_CUBE, MS_WEDGE, MS_CYL, MS_CYL8, MS_SPHERE, MS_TORUS, MS_SPIKE, MS_N };
static Mesh MESH[MS_N];

/* ================================================================== materials and draw lists */
typedef struct { v3 base; float metal, rough; v3 emis; int pattern; v3 trim; } Mat;
static const Mat MAT_FLOOR = {{0.14f, 0.15f, 0.17f}, 0.6f, 0.42f, {0, 0, 0}, 1, {0.1f, 0.9f, 1.6f}};
static const Mat MAT_WALL = {{0.20f, 0.21f, 0.24f}, 0.7f, 0.5f, {0, 0, 0}, 2, {1.8f, 0.55f, 0.15f}};
static const Mat MAT_WALL_C = {{0.22f, 0.22f, 0.26f}, 0.7f, 0.45f, {0, 0, 0}, 2, {0.1f, 1.0f, 1.8f}};
static const Mat MAT_CRATE = {{0.42f, 0.30f, 0.12f}, 0.6f, 0.55f, {0, 0, 0}, 2, {1.6f, 0.6f, 0.1f}};
static const Mat MAT_PILLAR = {{0.75f, 0.76f, 0.80f}, 1.0f, 0.22f, {0, 0, 0}, 0, {0, 0, 0}};
static const Mat MAT_DARK = {{0.06f, 0.06f, 0.07f}, 0.9f, 0.32f, {0, 0, 0}, 0, {0, 0, 0}};
static const Mat MAT_GLOW_C = {{0.02f, 0.02f, 0.02f}, 0, 0.5f, {0.2f, 2.2f, 4.0f}, 0, {0, 0, 0}};
static const Mat MAT_GLOW_O = {{0.02f, 0.02f, 0.02f}, 0, 0.5f, {4.0f, 1.3f, 0.25f}, 0, {0, 0, 0}};
static const Mat MAT_GLOW_M = {{0.02f, 0.02f, 0.02f}, 0, 0.5f, {3.2f, 0.4f, 2.6f}, 3, {0, 0, 0}};

typedef struct { int mesh; m4 model; Mat mat; bool shadow; } Draw;
#define MAXSTATIC 512
static Draw statics[MAXSTATIC];
static int nstatic;

/* collision volumes: boxes (optionally ramps whose top rises toward -z or +z etc) */
typedef struct { v3 mn, mx; int ramp; } Box; /* ramp: 0 none, 1 top rises toward -z, 2 toward +z, 3 toward -x, 4 toward +x */
#define MAXBOX 128
static Box boxes[MAXBOX];
static int nbox;

#define ARENA 30.0f
#define STEP 0.55f

static void add_static(int mesh, m4 model, Mat mat, bool shadow) {
    if (nstatic < MAXSTATIC) statics[nstatic++] = (Draw){mesh, model, mat, shadow};
}
static void add_box(v3 c, v3 size, Mat mat, bool collide) {
    add_static(MS_CUBE, m4_trs(c, 0, 0, 0, size), mat, true);
    if (collide && nbox < MAXBOX) boxes[nbox++] = (Box){sub(c, mul(size, 0.5f)), add(c, mul(size, 0.5f)), 0};
}
static float box_top(const Box *b, float x, float z) {
    float h = b->mx.y, lo = b->mn.y;
    switch (b->ramp) {
    case 1: return lo + (h - lo) * clampf((b->mx.z - z) / (b->mx.z - b->mn.z), 0, 1);
    case 2: return lo + (h - lo) * clampf((z - b->mn.z) / (b->mx.z - b->mn.z), 0, 1);
    case 3: return lo + (h - lo) * clampf((b->mx.x - x) / (b->mx.x - b->mn.x), 0, 1);
    case 4: return lo + (h - lo) * clampf((x - b->mn.x) / (b->mx.x - b->mn.x), 0, 1);
    }
    return h;
}
/* ramp: footprint centred at c, rising to height h toward direction dir (1..4) */
static void add_ramp(v3 c, float w, float l, float h, int dir) {
    float yaw = dir == 1 ? 0 : dir == 2 ? 3.14159f : dir == 3 ? -1.5708f : 1.5708f;
    bool alongz = dir <= 2;
    add_static(MS_WEDGE, m4_trs(c, yaw, 0, 0, V(w, h, l)), MAT_FLOOR, true);
    if (nbox < MAXBOX) {
        v3 half = alongz ? V(w / 2, 0, l / 2) : V(l / 2, 0, w / 2);
        boxes[nbox++] = (Box){V(c.x - half.x, c.y, c.z - half.z), V(c.x + half.x, c.y + h, c.z + half.z), dir};
    }
}

typedef struct { v3 pos, col; float radius; } Light;
#define MAXSTATICLIGHT 24
static Light static_lights[MAXSTATICLIGHT];
static int nstatic_light;
static v3 gates[4];

static void build_arena(void) {
    nstatic = nbox = nstatic_light = 0;
    /* floor */
    add_static(MS_CUBE, m4_trs(V(0, -0.5f, 0), 0, 0, 0, V(ARENA * 2 + 40, 1, ARENA * 2 + 40)), MAT_FLOOR, false);
    /* outer walls with gates in the middle of each side */
    for (int s = 0; s < 4; s++) {
        float sx = s == 0 ? 1.0f : s == 1 ? -1.0f : 0, sz = s == 2 ? 1.0f : s == 3 ? -1.0f : 0;
        for (int half = -1; half <= 1; half += 2) {
            float along = half * (ARENA + 4) / 2 + half * 2.5f * 0.5f;
            float seg = ARENA - 2.5f;
            v3 c = sx != 0 ? V(sx * (ARENA + 0.5f), 3.5f, along) : V(along, 3.5f, sz * (ARENA + 0.5f));
            v3 sz3 = sx != 0 ? V(1, 7, seg + 1) : V(seg + 1, 7, 1);
            add_box(c, sz3, s < 2 ? MAT_WALL : MAT_WALL_C, true);
        }
        v3 g = V(sx * (ARENA + 0.6f), 0, sz * (ARENA + 0.6f));
        gates[s] = V(sx * (ARENA - 2), 0, sz * (ARENA - 2));
        /* gate frame */
        v3 across = sx != 0 ? V(0, 0, 1) : V(1, 0, 0);
        add_box(add(g, add(mul(across, 2.6f), V(0, 3.5f, 0))), sx != 0 ? V(1.4f, 7, 0.6f) : V(0.6f, 7, 1.4f), MAT_DARK, true);
        add_box(add(g, add(mul(across, -2.6f), V(0, 3.5f, 0))), sx != 0 ? V(1.4f, 7, 0.6f) : V(0.6f, 7, 1.4f), MAT_DARK, true);
        add_box(add(g, V(0, 6.2f, 0)), sx != 0 ? V(1.4f, 0.5f, 5.8f) : V(5.8f, 0.5f, 1.4f), MAT_GLOW_M, false);
        add_box(add(g, V(0, 3.0f, 0)), sx != 0 ? V(0.3f, 6, 4.6f) : V(4.6f, 6, 0.3f), MAT_DARK, true);
        if (nstatic_light < MAXSTATICLIGHT)
            static_lights[nstatic_light++] = (Light){add(gates[s], V(0, 4, 0)), V(3.0f, 0.4f, 2.4f), 12};
    }
    /* corner towers */
    for (int k = 0; k < 4; k++) {
        float x = (k & 1 ? 1 : -1) * (ARENA - 2.5f), z = (k & 2 ? 1 : -1) * (ARENA - 2.5f);
        add_box(V(x, 6, z), V(5, 12, 5), MAT_WALL, true);
        add_box(V(x, 12.3f, z), V(5.4f, 0.6f, 5.4f), MAT_GLOW_O, false);
        if (nstatic_light < MAXSTATICLIGHT) static_lights[nstatic_light++] = (Light){V(x * 0.9f, 10, z * 0.9f), V(5, 1.6f, 0.4f), 22};
    }
    /* central platform with two ramps */
    add_box(V(0, 1.2f, 0), V(12, 2.4f, 12), MAT_WALL_C, true);
    add_box(V(0, 2.45f, 0), V(12.2f, 0.1f, 12.2f), MAT_FLOOR, false);
    add_ramp(V(0, 0, 9), 4, 6, 2.4f, 1);
    add_ramp(V(0, 0, -9), 4, 6, 2.4f, 2);
    add_box(V(0, 2.6f, 0), V(1.6f, 0.4f, 1.6f), MAT_GLOW_C, false);
    static_lights[nstatic_light++] = (Light){V(0, 4.5f, 0), V(0.6f, 3.0f, 5.0f), 14};
    /* pillars */
    for (int k = 0; k < 8; k++) {
        float a = k * 0.7854f + 0.39f, r = k & 1 ? 15.0f : 20.0f;
        float x = sinf(a) * r, z = cosf(a) * r;
        add_static(MS_CYL, m4_trs(V(x, 0, z), 0, 0, 0, V(1.6f, 8, 1.6f)), MAT_PILLAR, true);
        add_static(MS_CYL, m4_trs(V(x, 7.6f, z), 0, 0, 0, V(1.9f, 0.3f, 1.9f)), MAT_GLOW_C, false);
        if (nbox < MAXBOX) boxes[nbox++] = (Box){V(x - 0.7f, 0, z - 0.7f), V(x + 0.7f, 8, z + 0.7f), 0};
    }
    /* crates and low cover */
    static const float crates[][5] = {
        {-10, 6, 2.0f, 2.0f, 2.0f}, {-8.2f, 7.4f, 1.4f, 1.4f, 1.4f}, {11, -7, 2.4f, 1.6f, 2.4f}, {11, -7, 1.2f, 1.2f, 1.2f},
        {-16, -12, 2, 2, 2}, {17, 13, 2, 2, 2}, {18.6f, 11.2f, 1.4f, 1.4f, 1.4f}, {-22, 4, 1.6f, 1.6f, 4},
        {22, -3, 1.6f, 1.6f, 4}, {6, 21, 4, 1.4f, 1.6f}, {-6, -21, 4, 1.4f, 1.6f}, {-12, 18, 2.2f, 2.2f, 2.2f},
        {13, -19, 2.2f, 2.2f, 2.2f}, {-25, -22, 3, 3, 3},
    };
    for (unsigned k = 0; k < sizeof(crates) / sizeof(crates[0]); k++) {
        const float *c = crates[k];
        float base = 0;
        for (int b = 0; b < nbox; b++) /* stack on whatever is below */
            if (c[0] > boxes[b].mn.x && c[0] < boxes[b].mx.x && c[1] > boxes[b].mn.z && c[1] < boxes[b].mx.z && boxes[b].mx.y < 4)
                base = fmaxf(base, boxes[b].mx.y);
        add_box(V(c[0], base + c[3] / 2, c[1]), V(c[2], c[3], c[4]), MAT_CRATE, true);
    }
    for (int k = 0; k < 4; k++) { /* low walls */
        float a = k * 1.5708f + 0.785f;
        float x = sinf(a) * 10.5f, z = cosf(a) * 10.5f;
        bool alongx = k % 2 == 0;
        add_box(V(x, 0.6f, z), alongx ? V(5, 1.2f, 0.7f) : V(0.7f, 1.2f, 5), MAT_WALL, true);
        add_box(V(x, 1.22f, z), alongx ? V(5.05f, 0.05f, 0.75f) : V(0.75f, 0.05f, 5.05f), MAT_GLOW_O, false);
    }
    /* distant skyline beyond the walls, for scale */
    srand(7);
    for (int k = 0; k < 40; k++) {
        float a = k * 6.2832f / 40 + frs() * 0.05f, r = 70 + frand() * 60;
        float h = 10 + frand() * 45, w = 5 + frand() * 8;
        v3 c = V(sinf(a) * r, h / 2 - 2, cosf(a) * r);
        add_static(MS_CUBE, m4_trs(c, a, 0, 0, V(w, h, w)), MAT_WALL, false);
        if (k % 2) add_static(MS_CUBE, m4_trs(add(c, V(0, h / 2 + 0.2f, 0)), a, 0, 0, V(w * 0.3f, 0.4f, w * 0.3f)), k % 4 == 1 ? MAT_GLOW_M : MAT_GLOW_C, false);
    }
}

/* ================================================================== collision queries */
static float ground_at(float x, float z, float feet, float rad) {
    float best = 0;
    for (int i = 0; i < nbox; i++) {
        const Box *b = &boxes[i];
        if (x < b->mn.x - rad * 0.3f || x > b->mx.x + rad * 0.3f || z < b->mn.z - rad * 0.3f || z > b->mx.z + rad * 0.3f) continue;
        float top = box_top(b, clampf(x, b->mn.x, b->mx.x), clampf(z, b->mn.z, b->mx.z));
        if (top <= feet + STEP && top > best) best = top;
    }
    return best;
}
/* push a vertical cylinder out of every box it can't step onto */
static bool collide_cyl(v3 *p, float rad, float height) {
    bool hit = false;
    for (int it = 0; it < 2; it++)
        for (int i = 0; i < nbox; i++) {
            const Box *b = &boxes[i];
            if (p->y + height < b->mn.y || p->y > b->mx.y) continue;
            float cx = clampf(p->x, b->mn.x, b->mx.x), cz = clampf(p->z, b->mn.z, b->mx.z);
            if (box_top(b, cx, cz) <= p->y + STEP) continue;
            float dx = p->x - cx, dz = p->z - cz, d2 = dx * dx + dz * dz;
            if (d2 >= rad * rad) continue;
            hit = true;
            if (d2 > 1e-8f) {
                float d = sqrtf(d2);
                p->x = cx + dx / d * rad;
                p->z = cz + dz / d * rad;
            } else { /* centre inside: leave by the nearest face */
                float l = p->x - b->mn.x, r = b->mx.x - p->x, n = p->z - b->mn.z, f = b->mx.z - p->z;
                float m = fminf(fminf(l, r), fminf(n, f));
                if (m == l) p->x = b->mn.x - rad;
                else if (m == r) p->x = b->mx.x + rad;
                else if (m == n) p->z = b->mn.z - rad;
                else p->z = b->mx.z + rad;
            }
        }
    p->x = clampf(p->x, -ARENA + rad, ARENA - rad);
    p->z = clampf(p->z, -ARENA + rad, ARENA - rad);
    return hit;
}
/* ray vs world boxes: distance to first hit or maxd */
static float ray_world(v3 o, v3 d, float maxd) {
    float best = maxd;
    if (d.y < -1e-6f) { float t = -o.y / d.y; if (t > 0 && t < best) best = t; }
    for (int i = 0; i < nbox; i++) {
        const Box *b = &boxes[i];
        float t0 = 0, t1 = best;
        float lo[3] = {b->mn.x, b->mn.y, b->mn.z}, hi[3] = {b->mx.x, b->ramp ? b->mn.y + (b->mx.y - b->mn.y) * 0.6f : b->mx.y, b->mx.z};
        float oo[3] = {o.x, o.y, o.z}, dd[3] = {d.x, d.y, d.z};
        bool miss = false;
        for (int a = 0; a < 3 && !miss; a++) {
            if (fabsf(dd[a]) < 1e-8f) { if (oo[a] < lo[a] || oo[a] > hi[a]) miss = true; continue; }
            float ta = (lo[a] - oo[a]) / dd[a], tb = (hi[a] - oo[a]) / dd[a];
            if (ta > tb) { float s = ta; ta = tb; tb = s; }
            if (ta > t0) t0 = ta;
            if (tb < t1) t1 = tb;
            if (t0 > t1) miss = true;
        }
        if (!miss && t0 < best) best = t0;
    }
    return best;
}
static float ray_sphere(v3 o, v3 d, v3 c, float r) {
    v3 oc = sub(o, c);
    float b = dot(oc, d), cc = dot(oc, oc) - r * r, h = b * b - cc;
    if (h < 0) return -1;
    float t = -b - sqrtf(h);
    return t >= 0 ? t : (-b + sqrtf(h) >= 0 ? 0 : -1);
}

/* ================================================================== audio: a small software mixer */
#define RATE 48000
typedef struct { float *d; int n; } Snd;
typedef struct { const Snd *s; float pos, rate, gl, gr; bool loop, on; } Voice;
#define NVOICE 32
static Voice voices[NVOICE];
static SDL_AudioDeviceID adev;
static float master_vol = 0.8f, music_vol = 0.5f;
static bool muted;

enum { SN_RIFLE, SN_CANNON, SN_BOOM, SN_ZAP, SN_HIT, SN_KILL, SN_HURT, SN_PICK, SN_DASH, SN_JUMP, SN_RELOAD,
       SN_WAVE, SN_SCREECH, SN_SPAWN, SN_EMPTY, SN_CLEAR, SN_MUSIC, SN_N };
static Snd SND[SN_N];

static void audio_cb(void *u, Uint8 *stream, int bytes) {
    (void)u;
    float *out = (float *)stream;
    int n = bytes / 8;
    memset(out, 0, (size_t)bytes);
    for (int v = 0; v < NVOICE; v++) {
        Voice *vo = &voices[v];
        if (!vo->on) continue;
        for (int i = 0; i < n; i++) {
            int ip = (int)vo->pos;
            if (ip >= vo->s->n - 1) {
                if (vo->loop) { vo->pos -= (float)(vo->s->n - 1); ip = (int)vo->pos; }
                else { vo->on = false; break; }
            }
            float f = vo->pos - ip, smp = vo->s->d[ip] * (1 - f) + vo->s->d[ip + 1] * f;
            out[i * 2] += smp * vo->gl;
            out[i * 2 + 1] += smp * vo->gr;
            vo->pos += vo->rate;
        }
    }
    float mv = muted ? 0 : master_vol;
    for (int i = 0; i < n * 2; i++) out[i] = tanhf(out[i] * mv);
}

static v3 listener_pos, listener_right;
static void play3(int id, v3 pos, float vol, float pitch) {
    if (!adev) return;
    float g = vol, pan = 0;
    if (pos.y > -900) {
        v3 d = sub(pos, listener_pos);
        float dist = len(d);
        g = vol / (1 + dist * 0.09f);
        pan = dist > 0.01f ? dot(norm(d), listener_right) * 0.8f : 0;
    }
    SDL_LockAudioDevice(adev);
    int best = -1;
    for (int v = 0; v < NVOICE; v++)
        if (!voices[v].on) { best = v; break; }
    if (best < 0) { /* steal the oldest one-shot */
        float far = -1;
        for (int v = 0; v < NVOICE; v++)
            if (!voices[v].loop && voices[v].pos > far) { far = voices[v].pos; best = v; }
    }
    if (best >= 0) voices[best] = (Voice){&SND[id], 0, pitch, g * (1 - pan) * 0.7f, g * (1 + pan) * 0.7f, false, true};
    SDL_UnlockAudioDevice(adev);
}
static void play2(int id, float vol) { play3(id, V(0, -1000, 0), vol, 1); }

static float nz(void) { return frs(); }
static Snd snd_alloc(float secs) { Snd s; s.n = (int)(secs * RATE); s.d = calloc((size_t)s.n, sizeof(float)); return s; }

static void gen_sounds(void) {
    Snd s;
    float lp, ph;
    /* rifle: punchy noise crack + body thump */
    s = snd_alloc(0.22f); lp = 0; ph = 0;
    for (int i = 0; i < s.n; i++) {
        float t = (float)i / RATE;
        lp += (nz() - lp) * (0.9f * expf(-t * 18) + 0.08f);
        ph += (140 * expf(-t * 25) + 55) / RATE;
        s.d[i] = lp * expf(-t * 28) * 0.9f + sinf(ph * 6.2832f) * expf(-t * 30) * 0.8f + (i < 60 ? nz() * 0.6f : 0);
    }
    SND[SN_RIFLE] = s;
    /* cannon: descending energy sweep with sub */
    s = snd_alloc(0.7f); ph = 0; lp = 0;
    for (int i = 0; i < s.n; i++) {
        float t = (float)i / RATE;
        ph += (900 * expf(-t * 6) + 80) / RATE;
        lp += (nz() - lp) * 0.2f;
        s.d[i] = (sinf(ph * 6.2832f) * 0.6f + (fmodf(ph, 1) - 0.5f) * 0.4f) * expf(-t * 5) + lp * expf(-t * 12) * 0.5f;
    }
    SND[SN_CANNON] = s;
    /* explosion: low rumbling noise + sub drop */
    s = snd_alloc(1.6f); lp = 0; ph = 0;
    float lp2 = 0;
    for (int i = 0; i < s.n; i++) {
        float t = (float)i / RATE;
        lp += (nz() - lp) * (0.25f * expf(-t * 3) + 0.02f);
        lp2 += (lp - lp2) * 0.3f;
        ph += (70 * expf(-t * 4) + 28) / RATE;
        s.d[i] = lp2 * expf(-t * 2.6f) * 2.2f + sinf(ph * 6.2832f) * expf(-t * 4) * 0.9f + (i < 400 ? nz() * 0.7f * (1 - i / 400.0f) : 0);
    }
    SND[SN_BOOM] = s;
    /* enemy plasma zap */
    s = snd_alloc(0.25f); ph = 0;
    for (int i = 0; i < s.n; i++) {
        float t = (float)i / RATE;
        ph += (1500 * expf(-t * 9) + 300) / RATE;
        s.d[i] = ((fmodf(ph, 1) < 0.5f ? 1 : -1) * 0.35f + sinf(ph * 12.566f) * 0.2f) * expf(-t * 10);
    }
    SND[SN_ZAP] = s;
    /* hit marker tick */
    s = snd_alloc(0.06f);
    for (int i = 0; i < s.n; i++) { float t = (float)i / RATE; s.d[i] = sinf(t * 6.2832f * 2600) * expf(-t * 80) * 0.6f; }
    SND[SN_HIT] = s;
    /* kill confirm: two bright tones */
    s = snd_alloc(0.3f);
    for (int i = 0; i < s.n; i++) {
        float t = (float)i / RATE, f = t < 0.08f ? 1320 : 1980;
        s.d[i] = (sinf(t * 6.2832f * f) * 0.5f + sinf(t * 6.2832f * f * 2.01f) * 0.15f) * expf(-fmodf(t, 0.08f) * 20) * (t < 0.08f ? 1 : expf(-(t - 0.08f) * 12));
    }
    SND[SN_KILL] = s;
    /* player hurt: thud */
    s = snd_alloc(0.3f); lp = 0;
    for (int i = 0; i < s.n; i++) {
        float t = (float)i / RATE;
        lp += (nz() - lp) * 0.08f;
        s.d[i] = sinf(t * 6.2832f * (90 - t * 120)) * expf(-t * 14) * 0.9f + lp * expf(-t * 10) * 0.8f;
    }
    SND[SN_HURT] = s;
    /* pickup: rising arpeggio */
    s = snd_alloc(0.35f);
    for (int i = 0; i < s.n; i++) {
        float t = (float)i / RATE;
        int k = (int)(t / 0.07f);
        float f = 660 * powf(2, (k == 0 ? 0 : k == 1 ? 4 : k == 2 ? 7 : 12) / 12.0f);
        s.d[i] = sinf(t * 6.2832f * f) * 0.4f * expf(-fmodf(t, 0.07f) * 10) * (1 - t / 0.35f);
    }
    SND[SN_PICK] = s;
    /* dash whoosh */
    s = snd_alloc(0.35f); lp = 0;
    for (int i = 0; i < s.n; i++) {
        float t = (float)i / RATE, env = sinf(3.14159f * t / 0.35f);
        lp += (nz() - lp) * (0.05f + 0.25f * env);
        s.d[i] = lp * env * 1.2f;
    }
    SND[SN_DASH] = s;
    s = snd_alloc(0.12f); /* jump / land */
    for (int i = 0; i < s.n; i++) { float t = (float)i / RATE; s.d[i] = sinf(t * 6.2832f * (120 - t * 300)) * expf(-t * 30) * 0.6f; }
    SND[SN_JUMP] = s;
    s = snd_alloc(0.5f); /* reload: two mechanical clicks */
    for (int i = 0; i < s.n; i++) {
        float t = (float)i / RATE, a = 0;
        if (t < 0.04f) a = expf(-t * 120);
        else if (t > 0.32f && t < 0.38f) a = expf(-(t - 0.32f) * 100);
        s.d[i] = (nz() * 0.5f + sinf(t * 6.2832f * 1800) * 0.4f) * a;
    }
    SND[SN_RELOAD] = s;
    s = snd_alloc(1.4f); /* wave horn: detuned saw chord */
    {
        float ph3[3] = {0};
        float fr[3] = {110, 164.8f, 220.5f};
        lp = 0;
        for (int i = 0; i < s.n; i++) {
            float t = (float)i / RATE, env = fminf(t * 4, 1) * expf(-fmaxf(t - 0.6f, 0) * 3), v = 0;
            for (int k = 0; k < 3; k++) { ph3[k] += fr[k] / RATE; v += fmodf(ph3[k], 1) - 0.5f; }
            lp += (v - lp) * 0.06f;
            s.d[i] = lp * env * 0.9f;
        }
    }
    SND[SN_WAVE] = s;
    s = snd_alloc(0.3f); ph = 0; /* crawler screech */
    for (int i = 0; i < s.n; i++) {
        float t = (float)i / RATE;
        ph += (700 + 400 * sinf(t * 80)) / RATE;
        s.d[i] = (fmodf(ph, 1) - 0.5f) * 0.5f * sinf(3.14159f * t / 0.3f) + nz() * 0.1f * expf(-t * 10);
    }
    SND[SN_SCREECH] = s;
    s = snd_alloc(0.9f); ph = 0; /* spawn shimmer */
    for (int i = 0; i < s.n; i++) {
        float t = (float)i / RATE;
        ph += (300 + 900 * t) / RATE;
        s.d[i] = (sinf(ph * 6.2832f) + sinf(ph * 6.2832f * 1.5f) * 0.5f) * 0.25f * sinf(3.14159f * t / 0.9f);
    }
    SND[SN_SPAWN] = s;
    s = snd_alloc(0.05f);
    for (int i = 0; i < s.n; i++) { float t = (float)i / RATE; s.d[i] = sinf(t * 6.2832f * 900) * expf(-t * 90) * 0.4f; }
    SND[SN_EMPTY] = s;
    s = snd_alloc(1.2f); /* wave cleared: major arpeggio */
    for (int i = 0; i < s.n; i++) {
        float t = (float)i / RATE;
        int k = (int)(t / 0.12f);
        static const int st[] = {0, 4, 7, 12, 16, 19, 24, 24, 24, 24};
        float f = 440 * powf(2, st[k > 9 ? 9 : k] / 12.0f);
        s.d[i] = (sinf(t * 6.2832f * f) * 0.3f + sinf(t * 6.2832f * f * 2) * 0.08f) * expf(-fmodf(t, 0.12f) * 6) * (1 - t / 1.2f);
    }
    SND[SN_CLEAR] = s;
    /* music: a 32-second dark synthwave loop (A minor), rendered once */
    s = snd_alloc(32.0f);
    {
        const float bpm = 96, beat = 60 / bpm;
        static const int bass[16] = {0, 0, 12, 0, 0, 0, 10, 0, -4, -4, 8, -4, -2, -2, 10, -2};
        static const int chords[4][3] = {{0, 3, 7}, {-4, 0, 3}, {-7, -4, 0}, {-2, 2, 5}};
        float bph = 0, blp = 0, pph[3] = {0}, pph2[3] = {0}, plp = 0, alp = 0;
        float *dl = calloc(RATE, sizeof(float)); /* echo line */
        int di = 0;
        for (int i = 0; i < s.n; i++) {
            float t = (float)i / RATE;
            int b16 = (int)(t / (beat / 2)) % 16, bar = (int)(t / (beat * 4)) % 4;
            float tin = fmodf(t, beat / 2);
            float bf = 55 * powf(2, (bass[b16] + chords[bar][0] * 0) / 12.0f);
            if (bar == 1) bf *= powf(2, -4 / 12.0f);
            if (bar == 2) bf *= powf(2, -7 / 12.0f) * 2;
            if (bar == 3) bf *= powf(2, -2 / 12.0f);
            bph += bf / RATE;
            float saw = fmodf(bph, 1) * 2 - 1;
            blp += (saw - blp) * (0.02f + 0.12f * expf(-tin * 14));
            float bassv = blp * 0.55f * (0.5f + 0.5f * expf(-tin * 6));
            float pad = 0;
            for (int k = 0; k < 3; k++) {
                float f = 220 * powf(2, chords[bar][k] / 12.0f);
                pph[k] += f / RATE;
                pph2[k] += f * 1.006f / RATE;
                pad += (fmodf(pph[k], 1) - 0.5f) + (fmodf(pph2[k], 1) - 0.5f);
            }
            plp += (pad - plp) * (0.015f + 0.01f * sinf(t * 0.4f));
            int a16 = (int)(t / (beat / 4)) % 16;
            float atin = fmodf(t, beat / 4);
            float af = 440 * powf(2, (chords[bar][a16 % 3] + (a16 / 3 % 2) * 12) / 12.0f);
            float arp = sinf(t * 6.2832f * af) * expf(-atin * 18) * 0.12f * (bar % 2 ? 1 : 0.6f);
            alp += (arp - alp) * 0.5f;
            float hat = (b16 % 2 ? nz() * expf(-tin * 60) * 0.06f : 0);
            float kick = (b16 % 4 == 0) ? sinf(6.2832f * (50 * tin + 60 * (1 - expf(-tin * 30)) / 30)) * expf(-tin * 9) * 0.6f : 0;
            float dry = bassv + plp * 0.16f + alp + hat + kick;
            float wet = dl[di];
            dl[di] = (alp + plp * 0.05f) + wet * 0.45f;
            di = (di + 1) % (int)(beat * 0.75f * RATE);
            s.d[i] = (dry + wet * 0.5f) * 0.6f;
        }
        free(dl);
        for (int i = 0; i < 2000; i++) { /* smooth the loop seam */
            float k = i / 2000.0f;
            s.d[s.n - 2000 + i] = s.d[s.n - 2000 + i] * (1 - k) + s.d[i] * k;
        }
    }
    SND[SN_MUSIC] = s;
}
static int music_voice = -1;
static void start_music(void) {
    if (!adev) return;
    SDL_LockAudioDevice(adev);
    music_voice = NVOICE - 1;
    voices[music_voice] = (Voice){&SND[SN_MUSIC], 0, 1, music_vol * 0.5f, music_vol * 0.5f, true, true};
    SDL_UnlockAudioDevice(adev);
}

/* ================================================================== game state */
enum { EN_DRONE, EN_CRAWLER, EN_SENTINEL };
typedef struct {
    int type; bool alive;
    v3 pos, vel;
    float hp, maxhp, yaw, t, cool, flash, orbit, spawn_t, hurt_cool;
    int burst; bool split;
} Enemy;
typedef struct { bool on, enemy; v3 pos, vel; float life, dmg, radius; } Proj;
typedef struct { v3 pos, vel, col; float life, max, size; int kind; } Part;
typedef struct { v3 pos, vel, rot, spin; float life, size; Mat mat; } Debris;
typedef struct { bool on; v3 pos; float t; } Orb;
typedef struct { v3 a, b, col; float life; } Tracer;

#define MAXEN 64
#define MAXPROJ 128
#define MAXPART 3000
#define MAXDEB 160
#define MAXORB 24
#define MAXTR 32
static Enemy en[MAXEN];
static Proj proj[MAXPROJ];
static Part part[MAXPART];
static Debris deb[MAXDEB];
static Orb orbs[MAXORB];
static Tracer tr[MAXTR];
typedef struct { v3 pos, col; float radius, life, max; } DynLight;
#define MAXDL 32
static DynLight dl[MAXDL];

enum { ST_TITLE, ST_PLAY, ST_PAUSE, ST_DEAD };
static struct {
    int state;
    float t, st;
    v3 pos, vel;
    float yaw, pitch;
    bool ground;
    float hp, shield, regen_t, dash_cd, dash_t;
    int ammo;
    float reload_t, fire_cd, cannon_cd, energy;
    float recoil, bob, shake, damage_fx, hitmark, spread;
    v3 dash_dir;
    int wave, to_spawn[3], spawned, kills, score, best, combo;
    float combo_t, spawn_t, wave_t, msg_t;
    char msg[64];
    bool between;
    float hurt_dir[8], hurt_t[8];
    int shots, hits;
    float fov, sens;
    bool invert;
    float muzzle_t;
    int deaths_bot;
    float dmg_taken;
} G;
static bool selftest;
static char save_path[1024];

static void message(const char *m, float secs) {
    snprintf(G.msg, sizeof(G.msg), "%s", m);
    G.msg_t = secs;
}

static void add_light(v3 pos, v3 col, float radius, float life) {
    int best = 0;
    for (int i = 0; i < MAXDL; i++) {
        if (dl[i].life <= 0) { best = i; break; }
        if (dl[i].life < dl[best].life) best = i;
    }
    dl[best] = (DynLight){pos, col, radius, life, life};
}
static void spawn_part(v3 pos, v3 vel, v3 col, float life, float size, int kind) {
    static int next;
    for (int k = 0; k < MAXPART; k++) {
        int i = (next + k) % MAXPART;
        if (part[i].life <= 0) {
            part[i] = (Part){pos, vel, col, life, life, size, kind};
            next = i + 1;
            return;
        }
    }
}
static void sparks(v3 pos, v3 dir, int n, v3 col, float spd) {
    for (int i = 0; i < n; i++) {
        v3 v = add(mul(dir, spd * (0.3f + frand())), mul(V(frs(), frs() + 0.5f, frs()), spd * 0.6f));
        spawn_part(pos, v, col, 0.25f + frand() * 0.4f, 0.05f + frand() * 0.05f, 1);
    }
}
static void explosion(v3 pos, float scale, v3 col) {
    add_light(pos, mul(col, 14 * scale), 14 * scale, 0.5f);
    spawn_part(pos, V(0, 0, 0), mul(col, 3), 0.18f, 2.0f * scale, 0);
    spawn_part(pos, V(0, 0, 0), V(4, 3, 2), 0.12f, 1.0f * scale, 0);
    spawn_part(pos, V(0, 0, 0), mul(col, 3), 0.45f, 0.2f * scale, 2); /* shockwave ring (grows) */
    for (int i = 0; i < (int)(70 * scale); i++) {
        v3 v = mul(norm(V(frs(), frs() * 0.8f + 0.3f, frs())), (4 + frand() * 12) * scale);
        spawn_part(pos, v, frand() < 0.5f ? mul(col, 3) : V(5, 3, 1.2f), 0.4f + frand() * 0.7f, 0.06f + frand() * 0.08f, 1);
    }
    for (int i = 0; i < (int)(18 * scale); i++) {
        v3 v = mul(norm(V(frs(), frs() * 0.5f + 0.2f, frs())), 1 + frand() * 3);
        spawn_part(add(pos, mul(V(frs(), frs(), frs()), 0.5f * scale)), v, mul(col, 1.2f), 0.6f + frand() * 0.6f, (0.5f + frand() * 0.6f) * scale, 0);
    }
    float d = len(sub(pos, G.pos));
    G.shake = fmaxf(G.shake, clampf(1.2f * scale - d * 0.04f, 0, 1));
    play3(SN_BOOM, pos, 1.0f * scale, 0.9f + frand() * 0.2f);
}
static void debris(v3 pos, int n, Mat mat, float spd) {
    for (int k = 0; k < n; k++)
        for (int i = 0; i < MAXDEB; i++)
            if (deb[i].life <= 0) {
                deb[i] = (Debris){pos, add(V(frs() * spd, 2 + frand() * spd, frs() * spd), V(0, 0, 0)), V(frand() * 6, frand() * 6, 0),
                                  V(frs() * 9, frs() * 9, frs() * 9), 2.5f + frand() * 2, 0.15f + frand() * 0.25f, mat};
                break;
            }
}

/* ================================================================== enemies and waves */
static const Mat MAT_DRONE = {{0.12f, 0.13f, 0.15f}, 0.95f, 0.25f, {0, 0, 0}, 0, {0, 0, 0}};
static const Mat MAT_CRAWL = {{0.30f, 0.08f, 0.05f}, 0.8f, 0.35f, {0, 0, 0}, 0, {0, 0, 0}};
static const Mat MAT_SENT = {{0.55f, 0.56f, 0.60f}, 1.0f, 0.18f, {0, 0, 0}, 0, {0, 0, 0}};

static float en_radius(const Enemy *e) { return e->type == EN_DRONE ? 0.65f : e->type == EN_CRAWLER ? 0.75f : 1.6f; }
static v3 en_center(const Enemy *e) { return e->type == EN_CRAWLER ? add(e->pos, V(0, 0.55f, 0)) : e->pos; }

static Enemy *spawn_enemy(int type, v3 at) {
    for (int i = 0; i < MAXEN; i++)
        if (!en[i].alive) {
            Enemy e = {0};
            e.type = type;
            e.alive = true;
            e.pos = at;
            e.maxhp = e.hp = type == EN_DRONE ? 45 : type == EN_CRAWLER ? 38 : 480 + G.wave * 40;
            e.cool = 1.5f + frand() * 2;
            e.orbit = frand() * 6.28f;
            e.spawn_t = 0.8f;
            if (type == EN_DRONE) e.pos.y = 3 + frand() * 2;
            if (type == EN_SENTINEL) e.pos.y = 3;
            en[i] = e;
            v3 c = type == EN_CRAWLER ? V(4, 1.2f, 0.2f) : type == EN_DRONE ? V(0.3f, 2, 4) : V(4, 0.5f, 3);
            spawn_part(add(at, V(0, 3, 0)), V(0, 0, 0), mul(c, 2), 0.8f, 4, 0);
            for (int k = 0; k < 30; k++) spawn_part(add(at, V(frs() * 1.5f, frand() * 6, frs() * 1.5f)), V(0, 3 + frand() * 4, 0), c, 0.8f, 0.08f, 1);
            add_light(add(at, V(0, 2, 0)), mul(c, 3), 10, 0.9f);
            play3(SN_SPAWN, at, 0.8f, 1);
            return &en[i];
        }
    return NULL;
}

static int alive_count(void) {
    int n = 0;
    for (int i = 0; i < MAXEN; i++) n += en[i].alive;
    return n;
}

static void start_wave(int w) {
    G.wave = w;
    G.to_spawn[EN_DRONE] = 3 + w;
    G.to_spawn[EN_CRAWLER] = w >= 2 ? w * 2 + 1 : 0;
    G.to_spawn[EN_SENTINEL] = w % 4 == 0 ? 1 + (w >= 8) : 0;
    G.spawn_t = 0.5f;
    G.between = false;
    char m[32];
    snprintf(m, sizeof(m), "WAVE %d", w);
    message(m, 2.5f);
    play2(SN_WAVE, 0.8f);
}

static void damage_player(float dmg, v3 from) {
    if (G.state != ST_PLAY) return;
    if (G.dash_t > 0) dmg *= 0.3f;
    G.regen_t = 3.5f;
    G.dmg_taken += dmg;
    float s = fminf(G.shield, dmg);
    G.shield -= s;
    dmg -= s;
    G.hp -= dmg;
    G.damage_fx = fminf(1, G.damage_fx + 0.35f + dmg * 0.02f);
    G.shake = fmaxf(G.shake, 0.35f);
    v3 d = sub(from, G.pos);
    float ang = atan2f(d.x, -d.z) - G.yaw;
    for (int i = 0; i < 8; i++)
        if (G.hurt_t[i] <= 0) { G.hurt_dir[i] = ang; G.hurt_t[i] = 1.2f; break; }
    play2(SN_HURT, 0.8f);
    if (G.hp <= 0) {
        G.hp = 0;
        G.state = ST_DEAD;
        G.st = 0;
        if (G.score > G.best) G.best = G.score;
        if (!selftest && save_path[0]) {
            FILE *f = fopen(save_path, "w");
            if (f) { fprintf(f, "%d %.3f %.1f %d\n", G.best, G.sens, G.fov, G.invert); fclose(f); }
        }
        SDL_SetRelativeMouseMode(SDL_FALSE);
        message("", 0);
    }
}

static void kill_enemy(Enemy *e) {
    e->alive = false;
    G.kills++;
    G.combo++;
    G.combo_t = 3;
    int base = e->type == EN_DRONE ? 100 : e->type == EN_CRAWLER ? 80 : 1500;
    G.score += base * (1 + (G.combo - 1) / 3);
    v3 col = e->type == EN_CRAWLER ? V(4, 1.2f, 0.2f) : e->type == EN_DRONE ? V(0.4f, 1.6f, 4) : V(4, 0.6f, 3);
    explosion(en_center(e), e->type == EN_SENTINEL ? 2.2f : 1.0f, col);
    debris(en_center(e), e->type == EN_SENTINEL ? 16 : 6, e->type == EN_CRAWLER ? MAT_CRAWL : e->type == EN_DRONE ? MAT_DRONE : MAT_SENT, 5);
    play2(SN_KILL, 0.5f);
    if (frand() < (e->type == EN_SENTINEL ? 1.0f : 0.22f))
        for (int i = 0; i < MAXORB; i++)
            if (!orbs[i].on) { orbs[i] = (Orb){true, add(en_center(e), V(0, 0.3f, 0)), 0}; break; }
}

static void hurt_enemy(Enemy *e, float dmg, v3 at) {
    e->hp -= dmg;
    e->flash = 0.08f;
    if (e->type == EN_SENTINEL && !e->split && e->hp < e->maxhp * 0.5f) {
        e->split = true;
        for (int k = 0; k < 2; k++) spawn_enemy(EN_DRONE, add(e->pos, V(frs() * 3, 0, frs() * 3)));
    }
    (void)at;
    if (e->hp <= 0) kill_enemy(e);
}

static void enemy_fire(Enemy *e, v3 from, float speed, float spread) {
    v3 target = add(G.pos, V(0, 1.2f, 0));
    float t = len(sub(target, from)) / speed;
    target = add(target, mul(V(G.vel.x, 0, G.vel.z), t * (0.4f + frand() * 0.6f)));
    v3 d = norm(add(sub(target, from), mul(V(frs(), frs() * 0.5f, frs()), spread)));
    for (int i = 0; i < MAXPROJ; i++)
        if (!proj[i].on) {
            proj[i] = (Proj){true, true, from, mul(d, speed), 5, e->type == EN_SENTINEL ? 14.0f : 9.0f, 0.25f};
            break;
        }
    add_light(from, V(4, 0.4f, 2.5f), 6, 0.1f);
    play3(SN_ZAP, from, 0.6f, 0.9f + frand() * 0.2f);
}

static bool los(v3 a, v3 b) {
    v3 d = sub(b, a);
    float l = len(d);
    return ray_world(a, mul(d, 1 / l), l) >= l - 0.3f;
}

static void update_enemies(float dt) {
    v3 eye = add(G.pos, V(0, 1.6f, 0));
    for (int i = 0; i < MAXEN; i++) {
        Enemy *e = &en[i];
        if (!e->alive) continue;
        e->t += dt;
        e->flash -= dt;
        if (e->spawn_t > 0) { e->spawn_t -= dt; continue; }
        v3 to = sub(G.pos, e->pos);
        float dist = sqrtf(to.x * to.x + to.z * to.z);
        switch (e->type) {
        case EN_DRONE: {
            e->orbit += dt * (0.35f + 0.1f * (i % 3));
            float r = 9 + (i % 4) * 1.5f;
            v3 goal = V(G.pos.x + sinf(e->orbit) * r, 3.2f + sinf(e->t * 0.9f + i) * 1.2f + (i % 3) * 0.8f, G.pos.z + cosf(e->orbit) * r);
            v3 acc = mul(sub(goal, e->pos), 1.6f);
            for (int j = 0; j < MAXEN; j++) { /* separation */
                if (j == i || !en[j].alive) continue;
                v3 d = sub(e->pos, en[j].pos);
                float l = len(d);
                if (l < 2.5f && l > 0.01f) acc = add(acc, mul(d, (2.5f - l) * 3 / l));
            }
            e->vel = add(mul(e->vel, 1 - 2.2f * dt), mul(acc, dt));
            float sp = len(e->vel);
            if (sp > 8) e->vel = mul(e->vel, 8 / sp);
            v3 np = add(e->pos, mul(e->vel, dt));
            v3 test = V(np.x, np.y - 0.6f, np.z);
            if (collide_cyl(&test, 0.7f, 1.2f)) { np.x = test.x; np.z = test.z; }
            e->pos = np;
            e->yaw = atan2f(-to.x, -to.z);
            e->cool -= dt;
            if (e->cool <= 0 && G.state == ST_PLAY) {
                if (los(e->pos, eye)) enemy_fire(e, add(e->pos, mul(norm(sub(eye, e->pos)), 0.8f)), 20 + G.wave * 0.8f, 0.05f);
                e->cool = fmaxf(0.8f, 1.9f - G.wave * 0.1f) + frand() * 0.9f;
            }
            break;
        }
        case EN_CRAWLER: {
            v3 target = G.pos;
            if (G.pos.y > 2.0f && e->pos.y < 1.0f && fabsf(G.pos.x) < 6.5f && fabsf(G.pos.z) < 6.5f) /* player on the platform: take a ramp */
                target = V(0, 0, e->pos.z > 0 ? 12.5f : -12.5f);
            v3 d = sub(target, e->pos);
            float l = sqrtf(d.x * d.x + d.z * d.z);
            float spd = 7.0f + G.wave * 0.2f;
            v3 want = l > 0.01f ? V(d.x / l * spd, 0, d.z / l * spd) : V(0, 0, 0);
            e->vel.x += (want.x - e->vel.x) * fminf(1, 6 * dt);
            e->vel.z += (want.z - e->vel.z) * fminf(1, 6 * dt);
            v3 np = add(e->pos, V(e->vel.x * dt, 0, e->vel.z * dt));
            if (collide_cyl(&np, 0.6f, 1.0f)) { /* blocked: slide sideways */
                v3 side = V(-want.z, 0, want.x);
                if (((int)(e->t * 0.7f) + i) % 2) side = mul(side, -1);
                np = add(np, mul(side, dt * 0.6f));
                collide_cyl(&np, 0.6f, 1.0f);
            }
            for (int j = 0; j < MAXEN; j++) {
                if (j == i || !en[j].alive || en[j].type != EN_CRAWLER) continue;
                v3 dd = sub(np, en[j].pos);
                float ll = sqrtf(dd.x * dd.x + dd.z * dd.z);
                if (ll < 1.2f && ll > 0.01f) { np.x += dd.x / ll * (1.2f - ll) * 0.5f; np.z += dd.z / ll * (1.2f - ll) * 0.5f; }
            }
            float g = ground_at(np.x, np.z, e->pos.y, 0.6f);
            e->vel.y -= 20 * dt;
            np.y = e->pos.y + e->vel.y * dt;
            if (np.y <= g) { np.y = g; e->vel.y = 0; }
            e->pos = np;
            e->yaw = atan2f(-e->vel.x, -e->vel.z);
            e->hurt_cool -= dt;
            if (dist < 1.3f && fabsf(G.pos.y - e->pos.y) < 1.6f && e->hurt_cool <= 0 && G.state == ST_PLAY) {
                damage_player(14 + G.wave * 0.5f, e->pos);
                e->hurt_cool = 1.0f;
                e->vel = mul(norm(sub(e->pos, G.pos)), 7);
                play3(SN_SCREECH, e->pos, 0.7f, 1.1f);
            }
            if ((int)(e->t * 10) % 37 == 0 && frand() < 0.02f) play3(SN_SCREECH, e->pos, 0.4f, 0.8f + frand() * 0.4f);
            break;
        }
        case EN_SENTINEL: {
            float want_d = 14;
            v3 goal = dist > want_d ? G.pos : sub(e->pos, mul(V(to.x, 0, to.z), 0.5f));
            goal.y = 3 + sinf(e->t * 0.6f) * 0.8f;
            v3 acc = mul(sub(goal, e->pos), 0.6f);
            e->vel = add(mul(e->vel, 1 - 1.5f * dt), mul(acc, dt));
            float sp = len(e->vel);
            if (sp > 3) e->vel = mul(e->vel, 3 / sp);
            v3 np = add(e->pos, mul(e->vel, dt));
            v3 test = V(np.x, 0, np.z);
            collide_cyl(&test, 1.7f, 6);
            np.x = test.x;
            np.z = test.z;
            e->pos = np;
            e->yaw = atan2f(-to.x, -to.z);
            e->cool -= dt;
            if (e->cool <= 0 && G.state == ST_PLAY) {
                if (e->burst <= 0) e->burst = 5;
                if (los(e->pos, eye)) enemy_fire(e, add(e->pos, mul(norm(sub(eye, e->pos)), 1.8f)), 20, 0.12f);
                e->burst--;
                e->cool = e->burst > 0 ? 0.14f : 2.2f;
            }
            break;
        }
        }
    }
}

/* ================================================================== player */
typedef struct { float fwd, side, turn_x, turn_y; bool jump, sprint, fire, alt, dash, reload; } Input;
#define MAG 32

static void fire_rifle(void) {
    v3 eye = add(G.pos, V(0, 1.6f, 0));
    float cp = cosf(G.pitch);
    v3 fwd = V(sinf(G.yaw) * cp, sinf(G.pitch), -cosf(G.yaw) * cp);
    v3 right = V(cosf(G.yaw), 0, sinf(G.yaw)), up = cross(right, fwd);
    float spr = 0.006f + G.spread;
    v3 d = norm(add(fwd, add(mul(right, frs() * spr), mul(up, frs() * spr))));
    float tw = ray_world(eye, d, 150);
    Enemy *hit = NULL;
    float th = tw;
    for (int i = 0; i < MAXEN; i++) {
        if (!en[i].alive) continue;
        float t = ray_sphere(eye, d, en_center(&en[i]), en_radius(&en[i]));
        if (t >= 0 && t < th) { th = t; hit = &en[i]; }
    }
    v3 hp = add(eye, mul(d, th));
    G.shots++;
    if (hit) {
        G.hits++;
        hurt_enemy(hit, 13, hp);
        G.hitmark = 0.15f;
        play2(SN_HIT, 0.35f);
        sparks(hp, mul(d, -1), 10, V(4, 2.5f, 1), 6);
    } else if (th < 150) {
        sparks(hp, mul(d, -1), 6, V(3, 2.2f, 1.2f), 4);
        spawn_part(hp, V(0, 0, 0), V(2, 1.5f, 1), 0.15f, 0.35f, 0);
    }
    /* tracer from the muzzle (approximate world position of the barrel) */
    v3 muzzle = add(eye, add(mul(fwd, 0.9f), add(mul(right, 0.2f), mul(up, -0.16f))));
    for (int i = 0; i < MAXTR; i++)
        if (tr[i].life <= 0) { tr[i] = (Tracer){muzzle, hp, V(3, 1.8f, 0.6f), 0.06f}; break; }
    add_light(muzzle, V(6, 3.5f, 1.4f), 8, 0.06f);
    G.muzzle_t = 0.05f;
    G.recoil = fminf(G.recoil + 0.35f, 1.2f);
    G.pitch += 0.006f;
    G.yaw += frs() * 0.003f;
    G.spread = fminf(G.spread + 0.008f, 0.05f);
    play3(SN_RIFLE, muzzle, 0.55f, 0.95f + frand() * 0.1f);
}

static void fire_cannon(void) {
    v3 eye = add(G.pos, V(0, 1.6f, 0));
    float cp = cosf(G.pitch);
    v3 fwd = V(sinf(G.yaw) * cp, sinf(G.pitch), -cosf(G.yaw) * cp);
    for (int i = 0; i < MAXPROJ; i++)
        if (!proj[i].on) {
            proj[i] = (Proj){true, false, add(eye, mul(fwd, 0.8f)), mul(fwd, 38), 3, 90, 0.3f};
            break;
        }
    G.recoil = 1.6f;
    G.energy -= 1;
    G.cannon_cd = 0.9f;
    G.shake = fmaxf(G.shake, 0.25f);
    play3(SN_CANNON, eye, 0.9f, 1);
}

static void update_player(const Input *in, float dt) {
    G.yaw += in->turn_x;
    G.pitch = clampf(G.pitch + in->turn_y, -1.45f, 1.45f);
    float sy = sinf(G.yaw), cy = cosf(G.yaw);
    v3 fwd = V(sy, 0, -cy), right = V(cy, 0, sy);
    v3 wish = add(mul(fwd, in->fwd), mul(right, in->side));
    float wl = len(wish);
    if (wl > 1) wish = mul(wish, 1 / wl);
    float speed = in->sprint ? 9.5f : 6.5f;
    G.dash_cd -= dt;
    if (in->dash && G.dash_cd <= 0) {
        G.dash_dir = wl > 0.1f ? norm(wish) : fwd;
        G.dash_t = 0.18f;
        G.dash_cd = 1.4f;
        play2(SN_DASH, 0.6f);
    }
    if (G.dash_t > 0) {
        G.dash_t -= dt;
        G.vel.x = G.dash_dir.x * 26;
        G.vel.z = G.dash_dir.z * 26;
    } else {
        float acc = G.ground ? 14 : 3;
        G.vel.x += (wish.x * speed - G.vel.x) * fminf(1, acc * dt);
        G.vel.z += (wish.z * speed - G.vel.z) * fminf(1, acc * dt);
    }
    if (in->jump && G.ground) { G.vel.y = 6.2f; G.ground = false; play2(SN_JUMP, 0.4f); }
    G.vel.y -= 18 * dt;
    v3 np = add(G.pos, V(G.vel.x * dt, 0, G.vel.z * dt));
    collide_cyl(&np, 0.4f, 1.8f);
    float g = ground_at(np.x, np.z, G.pos.y, 0.4f);
    np.y = G.pos.y + G.vel.y * dt;
    bool was = G.ground;
    if (np.y <= g) {
        if (!was && G.vel.y < -6) play2(SN_JUMP, 0.3f);
        np.y = g;
        G.vel.y = 0;
        G.ground = true;
    } else if (np.y > g + 0.05f) G.ground = false;
    if (G.ground && np.y < g + STEP) np.y = g; /* walk down ramps smoothly */
    G.pos = np;
    float hs = sqrtf(G.vel.x * G.vel.x + G.vel.z * G.vel.z);
    if (G.ground) G.bob += hs * dt * 1.6f;

    /* weapons */
    G.fire_cd -= dt;
    G.cannon_cd -= dt;
    G.spread = fmaxf(0, G.spread - dt * 0.12f);
    G.energy = fminf(3, G.energy + dt * 0.33f);
    if (G.reload_t > 0) {
        G.reload_t -= dt;
        if (G.reload_t <= 0) G.ammo = MAG;
    } else if ((in->reload && G.ammo < MAG) || G.ammo == 0) {
        G.reload_t = 1.3f;
        play2(SN_RELOAD, 0.6f);
    }
    if (in->fire && G.fire_cd <= 0 && G.reload_t <= 0 && G.ammo > 0) {
        fire_rifle();
        G.ammo--;
        G.fire_cd = 0.085f;
    }
    if (in->alt && G.cannon_cd <= 0) {
        if (G.energy >= 1) fire_cannon();
        else { play2(SN_EMPTY, 0.5f); G.cannon_cd = 0.3f; }
    }
    /* shield regeneration */
    G.regen_t -= dt;
    if (G.regen_t <= 0) G.shield = fminf(50, G.shield + 22 * dt);
    /* health orbs: drift toward the player when near */
    for (int i = 0; i < MAXORB; i++) {
        Orb *o = &orbs[i];
        if (!o->on) continue;
        o->t += dt;
        v3 tgt = add(G.pos, V(0, 1, 0));
        v3 d = sub(tgt, o->pos);
        float l = len(d);
        if (l < 6) o->pos = add(o->pos, mul(d, fminf(1, dt * (8 / fmaxf(l, 0.5f)))));
        else o->pos.y += (1.0f + sinf(o->t * 2) * 0.2f - o->pos.y) * dt;
        if (l < 1.0f && G.hp < 100) {
            o->on = false;
            G.hp = fminf(100, G.hp + 25);
            play2(SN_PICK, 0.6f);
            add_light(o->pos, V(0.4f, 4, 1), 6, 0.3f);
        }
        if (o->t > 20) o->on = false;
    }
}

static void update_world(float dt) {
    /* projectiles */
    for (int i = 0; i < MAXPROJ; i++) {
        Proj *p = &proj[i];
        if (!p->on) continue;
        p->life -= dt;
        v3 d = mul(p->vel, dt);
        float l = len(d);
        v3 dir = mul(d, 1 / l);
        float tw = ray_world(p->pos, dir, l);
        bool boom = tw < l || p->life <= 0;
        v3 hitp = add(p->pos, mul(dir, tw));
        if (p->enemy) {
            v3 pc = add(G.pos, V(0, 0.9f, 0));
            float t = ray_sphere(p->pos, dir, pc, 0.7f);
            if (t >= 0 && t < l && G.state == ST_PLAY) {
                damage_player(p->dmg, p->pos);
                p->on = false;
                sparks(add(p->pos, mul(dir, t)), mul(dir, -1), 12, V(4, 0.6f, 3), 4);
                continue;
            }
            if (boom) {
                p->on = false;
                sparks(hitp, mul(dir, -1), 10, V(4, 0.5f, 3), 4);
                add_light(hitp, V(3, 0.3f, 2), 5, 0.15f);
                continue;
            }
            p->pos = add(p->pos, d);
            spawn_part(p->pos, V(0, 0, 0), V(3, 0.4f, 2.4f), 0.12f, 0.18f, 0);
        } else {
            for (int k = 0; k < MAXEN && !boom; k++) {
                if (!en[k].alive) continue;
                float t = ray_sphere(p->pos, dir, en_center(&en[k]), en_radius(&en[k]) + 0.2f);
                if (t >= 0 && t < l) { boom = true; hitp = add(p->pos, mul(dir, t)); }
            }
            if (boom) {
                p->on = false;
                explosion(hitp, 1.2f, V(0.4f, 2, 4));
                for (int k = 0; k < MAXEN; k++) {
                    if (!en[k].alive) continue;
                    float dd = len(sub(en_center(&en[k]), hitp));
                    if (dd < 4.5f) { hurt_enemy(&en[k], p->dmg * (1 - dd / 6), hitp); G.hitmark = 0.2f; }
                }
                float pd = len(sub(add(G.pos, V(0, 1, 0)), hitp));
                if (pd < 3) damage_player(20 * (1 - pd / 3), hitp);
                continue;
            }
            p->pos = add(p->pos, d);
            spawn_part(p->pos, V(0, 0, 0), V(0.5f, 2.5f, 5), 0.2f, 0.5f, 0);
            spawn_part(p->pos, V(frs(), frs(), frs()), V(0.3f, 1.5f, 4), 0.4f, 0.06f, 1);
        }
    }
    for (int i = 0; i < MAXPART; i++) {
        Part *q = &part[i];
        if (q->life <= 0) continue;
        q->life -= dt;
        q->pos = add(q->pos, mul(q->vel, dt));
        if (q->kind == 1) {
            q->vel.y -= 9 * dt;
            if (q->pos.y < 0.02f && q->vel.y < 0) { q->pos.y = 0.02f; q->vel.y *= -0.35f; q->vel.x *= 0.6f; q->vel.z *= 0.6f; }
        } else q->vel = mul(q->vel, 1 - 2 * dt);
    }
    for (int i = 0; i < MAXDEB; i++) {
        Debris *b = &deb[i];
        if (b->life <= 0) continue;
        b->life -= dt;
        b->vel.y -= 15 * dt;
        b->pos = add(b->pos, mul(b->vel, dt));
        b->rot = add(b->rot, mul(b->spin, dt));
        float g = ground_at(b->pos.x, b->pos.z, b->pos.y + 0.5f, 0);
        if (b->pos.y < g + b->size * 0.5f) {
            b->pos.y = g + b->size * 0.5f;
            b->vel = V(b->vel.x * 0.6f, -b->vel.y * 0.3f, b->vel.z * 0.6f);
            b->spin = mul(b->spin, 0.5f);
        }
    }
    for (int i = 0; i < MAXTR; i++) tr[i].life -= dt;
    for (int i = 0; i < MAXDL; i++) dl[i].life -= dt;
}

static void update_waves(float dt) {
    G.combo_t -= dt;
    if (G.combo_t <= 0) G.combo = 0;
    if (G.between) {
        G.wave_t -= dt;
        if (G.wave_t <= 0) start_wave(G.wave + 1);
        return;
    }
    int remaining = G.to_spawn[0] + G.to_spawn[1] + G.to_spawn[2];
    if (remaining > 0) {
        G.spawn_t -= dt;
        if (G.spawn_t <= 0 && alive_count() < 14) {
            int type = G.to_spawn[EN_SENTINEL] > 0 && G.to_spawn[EN_DRONE] < 3 ? EN_SENTINEL
                     : G.to_spawn[EN_CRAWLER] > 0 && (rand() % 2 || !G.to_spawn[EN_DRONE]) ? EN_CRAWLER
                     : G.to_spawn[EN_DRONE] > 0 ? EN_DRONE : EN_SENTINEL;
            if (G.to_spawn[type] <= 0) for (type = 0; type < 3 && G.to_spawn[type] <= 0; type++) {}
            /* use the gate farthest from the player */
            int gi = 0;
            float far = -1;
            for (int k = 0; k < 4; k++) {
                float d = len(sub(gates[k], G.pos)) + frand() * 12;
                if (d > far) { far = d; gi = k; }
            }
            spawn_enemy(type, add(gates[gi], V(frs() * 2, 0, frs() * 2)));
            G.to_spawn[type]--;
            G.spawn_t = fmaxf(0.35f, 1.4f - G.wave * 0.08f);
        }
    } else if (alive_count() == 0) {
        G.between = true;
        G.wave_t = 5;
        G.score += 500 * G.wave;
        char m[48];
        snprintf(m, sizeof(m), "WAVE %d CLEARED  +%d", G.wave, 500 * G.wave);
        message(m, 3.5f);
        play2(SN_CLEAR, 0.7f);
        G.shield = 50;
    }
}

static void new_game(void) {
    memset(en, 0, sizeof(en));
    memset(proj, 0, sizeof(proj));
    memset(part, 0, sizeof(part));
    memset(deb, 0, sizeof(deb));
    memset(orbs, 0, sizeof(orbs));
    memset(tr, 0, sizeof(tr));
    memset(dl, 0, sizeof(dl));
    G.pos = V(0, 2.4f, 4);
    G.vel = V(0, 0, 0);
    G.yaw = 3.14159f;
    G.pitch = -0.05f;
    G.hp = 100;
    G.shield = 50;
    G.ammo = MAG;
    G.energy = 3;
    G.reload_t = G.fire_cd = G.cannon_cd = G.dash_cd = G.dash_t = 0;
    G.kills = G.score = G.combo = G.shots = G.hits = 0;
    G.damage_fx = G.shake = G.recoil = 0;
    for (int i = 0; i < 8; i++) G.hurt_t[i] = 0;
    G.state = ST_PLAY;
    G.st = 0;
    start_wave(1);
}

/* ================================================================== renderer */
static GLuint P_LIT, P_DEPTH, P_SKY, P_PART, P_DOWN, P_UP, P_COMP, P_HUD;
static GLuint fbo_shadow, tex_shadow;
static GLuint fbo_msaa, rb_msaa_col, rb_msaa_dep, fbo_hdr, tex_hdr, fbo_ldr, tex_ldr;
#define NMIP 6
static GLuint fbo_mip[NMIP], tex_mip[NMIP];
static int mip_w[NMIP], mip_h[NMIP];
static int rw, rh, msaa_samples = 4;
static GLuint vao_empty, vao_part, vbo_part, vao_hud, vbo_hud, tex_font;
static const int SHADOW_RES = 2048;
static v3 SUN_DIR, SUN_COL;
static m4 light_vp;

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

/* the 5x7 pixel font, uploaded as a texture atlas */
static const char FONT_CHARS[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ:.-!/?, +<>'()=*%";
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
    MESH[MS_CYL] = mk_cyl(20, true);
    MESH[MS_CYL8] = mk_cyl(8, false);
    MESH[MS_SPHERE] = mk_sphere(24, 16);
    MESH[MS_TORUS] = mk_torus(28, 8, 1, 0.12f);
    MESH[MS_SPIKE] = mk_spike();
    glGenVertexArrays(1, &vao_empty);
    /* shadow map */
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
    /* dynamic vertex buffers */
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
    SUN_DIR = norm(V(-0.55f, 0.32f, -0.78f));
    SUN_COL = V(1.25f, 0.95f, 1.15f);
    v3 c = V(0, 0, 0);
    light_vp = m4_mul(m4_ortho(-48, 48, -48, 48, 1, 200), m4_look(add(c, mul(SUN_DIR, 90)), c, V(0, 1, 0)));
}

/* ---- per-frame draw helpers */
typedef struct { m4 view, proj, vp; v3 pos, right, up, fwd; } Cam;
static Cam cam;
static Light frame_lights[32];
static int nframe_lights;

static void gather_lights(void) {
    nframe_lights = 0;
    /* dynamic first (they matter most), then static, nearest to the camera */
    Light cand[MAXDL + MAXSTATICLIGHT + MAXEN + MAXPROJ];
    float score[MAXDL + MAXSTATICLIGHT + MAXEN + MAXPROJ];
    int n = 0;
    for (int i = 0; i < MAXDL; i++)
        if (dl[i].life > 0) {
            float k = dl[i].life / dl[i].max;
            cand[n] = (Light){dl[i].pos, mul(dl[i].col, k), dl[i].radius};
            score[n++] = -1000 + len(sub(dl[i].pos, cam.pos));
        }
    for (int i = 0; i < MAXEN; i++)
        if (en[i].alive && en[i].type != EN_CRAWLER) {
            v3 c = en[i].type == EN_DRONE ? V(0.3f, 1.4f, 3) : V(3, 0.4f, 2.4f);
            cand[n] = (Light){en_center(&en[i]), c, en[i].type == EN_DRONE ? 5.0f : 9.0f};
            score[n++] = len(sub(en[i].pos, cam.pos));
        }
    for (int i = 0; i < MAXPROJ; i++)
        if (proj[i].on) {
            cand[n] = (Light){proj[i].pos, proj[i].enemy ? V(3, 0.3f, 2) : V(0.6f, 2.5f, 5), proj[i].enemy ? 4.0f : 7.0f};
            score[n++] = -500 + len(sub(proj[i].pos, cam.pos));
        }
    for (int i = 0; i < nstatic_light; i++) {
        cand[n] = static_lights[i];
        score[n++] = len(sub(static_lights[i].pos, cam.pos)) * 0.5f;
    }
    for (int k = 0; k < 32 && k < n; k++) {
        int best = k;
        for (int j = k + 1; j < n; j++) if (score[j] < score[best]) best = j;
        Light tl = cand[k]; cand[k] = cand[best]; cand[best] = tl;
        float ts = score[k]; score[k] = score[best]; score[best] = ts;
        frame_lights[nframe_lights++] = cand[k];
    }
}

static GLint uL_model, uL_vp, uL_lvp, uL_cam, uL_base, uL_emis, uL_trim, uL_metal, uL_rough, uL_pattern, uL_flash;
static void lit_begin(m4 vp, float time) {
    glUseProgram(P_LIT);
    uL_model = U(P_LIT, "uModel"); uL_vp = U(P_LIT, "uVP"); uL_lvp = U(P_LIT, "uLightVP");
    uL_cam = U(P_LIT, "uCam"); uL_base = U(P_LIT, "uBase"); uL_emis = U(P_LIT, "uEmis"); uL_trim = U(P_LIT, "uTrim");
    uL_metal = U(P_LIT, "uMetal"); uL_rough = U(P_LIT, "uRough"); uL_pattern = U(P_LIT, "uPattern"); uL_flash = U(P_LIT, "uFlash");
    glUniformMatrix4fv(uL_vp, 1, GL_FALSE, vp.m);
    glUniformMatrix4fv(uL_lvp, 1, GL_FALSE, light_vp.m);
    glUniform3f(uL_cam, cam.pos.x, cam.pos.y, cam.pos.z);
    glUniform3f(U(P_LIT, "uSunDir"), SUN_DIR.x, SUN_DIR.y, SUN_DIR.z);
    glUniform3f(U(P_LIT, "uSunCol"), SUN_COL.x, SUN_COL.y, SUN_COL.z);
    glUniform3f(U(P_LIT, "uFogCol"), 0.06f, 0.03f, 0.07f);
    glUniform1f(U(P_LIT, "uFogDen"), 0.0085f);
    glUniform1f(U(P_LIT, "uTime"), time);
    glUniform1i(U(P_LIT, "uShadow"), 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex_shadow);
    float lp[32 * 4], lc[32 * 3];
    for (int i = 0; i < nframe_lights; i++) {
        lp[i * 4] = frame_lights[i].pos.x; lp[i * 4 + 1] = frame_lights[i].pos.y; lp[i * 4 + 2] = frame_lights[i].pos.z;
        lp[i * 4 + 3] = frame_lights[i].radius;
        lc[i * 3] = frame_lights[i].col.x; lc[i * 3 + 1] = frame_lights[i].col.y; lc[i * 3 + 2] = frame_lights[i].col.z;
    }
    glUniform1i(U(P_LIT, "uNumLights"), nframe_lights);
    if (nframe_lights) {
        glUniform4fv(U(P_LIT, "uLightPos"), nframe_lights, lp);
        glUniform3fv(U(P_LIT, "uLightCol"), nframe_lights, lc);
    }
}
static void lit_draw(int mesh, m4 model, const Mat *m, float flash) {
    glUniformMatrix4fv(uL_model, 1, GL_FALSE, model.m);
    glUniform3f(uL_base, m->base.x, m->base.y, m->base.z);
    glUniform3f(uL_emis, m->emis.x, m->emis.y, m->emis.z);
    glUniform3f(uL_trim, m->trim.x, m->trim.y, m->trim.z);
    glUniform1f(uL_metal, m->metal);
    glUniform1f(uL_rough, m->rough);
    glUniform1i(uL_pattern, m->pattern);
    glUniform1f(uL_flash, flash);
    glBindVertexArray(MESH[mesh].vao);
    glDrawArrays(GL_TRIANGLES, 0, MESH[mesh].count);
}

/* enemies, debris and orbs are drawn through one callback so the shadow pass sees them too */
typedef void (*DrawFn)(int mesh, m4 model, const Mat *m, float flash);
static GLint uD_model;
static void depth_draw(int mesh, m4 model, const Mat *m, float flash) {
    (void)m; (void)flash;
    glUniformMatrix4fv(uD_model, 1, GL_FALSE, model.m);
    glBindVertexArray(MESH[mesh].vao);
    glDrawArrays(GL_TRIANGLES, 0, MESH[mesh].count);
}

static void draw_dynamic(DrawFn fn, float time) {
    for (int i = 0; i < MAXEN; i++) {
        Enemy *e = &en[i];
        if (!e->alive) continue;
        float fl = e->flash > 0 ? 3.0f : 0;
        float grow = e->spawn_t > 0 ? 1 - e->spawn_t / 0.8f : 1;
        if (grow < 0.05f) continue;
        switch (e->type) {
        case EN_DRONE: {
            v3 p = e->pos;
            float tilt = clampf(e->vel.x * 0.05f, -0.4f, 0.4f);
            fn(MS_SPHERE, m4_trs(p, e->yaw, 0, tilt, mul(V(0.55f, 0.48f, 0.55f), grow)), &MAT_DRONE, fl);
            fn(MS_TORUS, m4_trs(p, e->yaw + time * 2, 0.25f, tilt, mul(V(0.85f, 0.85f, 0.85f), grow)), &MAT_SENT, fl);
            Mat eye = MAT_GLOW_O;
            eye.emis = e->cool < 0.4f ? V(8, 2, 0.5f) : V(5, 1.2f, 0.3f);
            v3 front = V(-sinf(e->yaw) * 0.45f, 0, -cosf(e->yaw) * 0.45f);
            fn(MS_SPHERE, m4_trs(add(p, front), e->yaw, 0, 0, mul(V(0.17f, 0.17f, 0.17f), grow)), &eye, 0);
            for (int k = -1; k <= 1; k += 2) {
                v3 side = V(cosf(e->yaw) * 0.62f * k, 0, -sinf(e->yaw) * 0.62f * k);
                fn(MS_CUBE, m4_trs(add(p, side), e->yaw, 0, tilt, mul(V(0.12f, 0.5f, 0.6f), grow)), &MAT_DRONE, fl);
                fn(MS_CUBE, m4_trs(add(add(p, side), V(0, -0.27f, 0)), e->yaw, 0, tilt, mul(V(0.14f, 0.04f, 0.5f), grow)), &MAT_GLOW_C, 0);
            }
            break;
        }
        case EN_CRAWLER: {
            v3 p = e->pos;
            float step = sinf(e->t * 18) * 0.3f;
            fn(MS_CUBE, m4_trs(add(p, V(0, 0.6f, 0)), e->yaw, 0.08f, 0, mul(V(0.9f, 0.45f, 1.3f), grow)), &MAT_CRAWL, fl);
            fn(MS_CUBE, m4_trs(add(p, V(0, 0.86f, 0)), e->yaw, 0.08f, 0, mul(V(0.2f, 0.08f, 1.2f), grow)), &MAT_GLOW_O, 0);
            v3 fwd = V(-sinf(e->yaw), 0, -cosf(e->yaw)), rt = V(cosf(e->yaw), 0, -sinf(e->yaw));
            fn(MS_SPIKE, m4_trs(add(p, add(mul(fwd, 0.75f), V(0, 0.55f, 0))), e->yaw, -1.5708f, 0, mul(V(0.5f, 0.6f, 0.3f), grow)), &MAT_SENT, fl);
            for (int l = 0; l < 4; l++) {
                float side = l & 1 ? 1.0f : -1.0f, fz = l & 2 ? 0.4f : -0.4f;
                float ph = (l == 0 || l == 3) ? step : -step;
                v3 lp = add(p, add(mul(rt, side * 0.6f), add(mul(fwd, fz + ph * 0.5f), V(0, 0.3f, 0))));
                fn(MS_CUBE, m4_trs(lp, e->yaw, ph, side * 0.6f, mul(V(0.12f, 0.6f, 0.12f), grow)), &MAT_DRONE, fl);
            }
            break;
        }
        case EN_SENTINEL: {
            v3 p = e->pos;
            fn(MS_SPHERE, m4_trs(p, e->yaw, 0, 0, mul(V(1.2f, 1.2f, 1.2f), grow)), &MAT_SENT, fl);
            Mat core = MAT_GLOW_M;
            core.emis = mul(V(4, 0.5f, 3), 1 + (e->burst > 0 ? 1.5f : 0));
            v3 front = V(-sinf(e->yaw) * 1.05f, 0, -cosf(e->yaw) * 1.05f);
            fn(MS_SPHERE, m4_trs(add(p, front), e->yaw, 0, 0, mul(V(0.42f, 0.42f, 0.42f), grow)), &core, 0);
            fn(MS_TORUS, m4_trs(p, time * 0.8f, 1.2f, 0, mul(V(2.1f, 2.1f, 2.1f), grow)), &MAT_DRONE, fl);
            fn(MS_TORUS, m4_trs(p, -time * 0.6f, -0.5f, 0.9f, mul(V(1.8f, 1.8f, 1.8f), grow)), &MAT_GLOW_M, 0);
            for (int k = 0; k < 3; k++) {
                float a = time * 1.1f + k * 2.094f;
                v3 o = V(sinf(a) * 2.3f, sinf(time * 2 + k) * 0.4f, cosf(a) * 2.3f);
                fn(MS_CUBE, m4_trs(add(p, o), a, 0.3f, 0, mul(V(0.9f, 1.4f, 0.25f), grow)), &MAT_DRONE, fl);
            }
            break;
        }
        }
    }
    for (int i = 0; i < MAXDEB; i++)
        if (deb[i].life > 0) {
            float s = deb[i].size * fminf(1, deb[i].life);
            fn(MS_CUBE, m4_trs(deb[i].pos, deb[i].rot.x, deb[i].rot.y, deb[i].rot.z, V(s, s * 0.6f, s * 1.3f)), &deb[i].mat, 0);
        }
    for (int i = 0; i < MAXORB; i++)
        if (orbs[i].on) {
            Mat m = MAT_GLOW_C;
            m.emis = V(0.4f, 4.0f, 1.2f);
            float s = 0.22f + sinf(orbs[i].t * 6) * 0.03f;
            fn(MS_SPHERE, m4_trs(orbs[i].pos, 0, 0, 0, V(s, s, s)), &m, 0);
            fn(MS_TORUS, m4_trs(orbs[i].pos, orbs[i].t * 3, 1.2f, 0, V(0.4f, 0.4f, 0.4f)), &m, 0);
        }
}

/* particles: CPU-built camera-facing quads, additive */
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
static void flush_particles(m4 vp) {
    if (!pcount) return;
    glUseProgram(P_PART);
    glUniformMatrix4fv(U(P_PART, "uVP"), 1, GL_FALSE, vp.m);
    glBindVertexArray(vao_part);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_part);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * (size_t)pcount * 10, pbuf, GL_STREAM_DRAW);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glDepthMask(GL_FALSE);
    glDrawArrays(GL_TRIANGLES, 0, pcount);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    pcount = 0;
}
static void build_particles(float time) {
    for (int i = 0; i < MAXPART; i++) {
        Part *q = &part[i];
        if (q->life <= 0) continue;
        float k = q->life / q->max;
        if (q->kind == 1) { /* sparks stretch along their velocity */
            v3 tail = sub(q->pos, mul(q->vel, 0.025f));
            beam(tail, q->pos, q->size * 0.6f, q->col, k);
        } else if (q->kind == 2) {
            billboard(q->pos, (1 - k) * 9 * q->size * 5 + 0.5f, q->col, k, 2);
        } else {
            billboard(q->pos, q->size * (0.6f + 0.4f * k), q->col, k, 0);
        }
    }
    for (int i = 0; i < MAXTR; i++)
        if (tr[i].life > 0) beam(tr[i].a, tr[i].b, 0.035f, tr[i].col, tr[i].life / 0.06f);
    for (int i = 0; i < MAXEN; i++) { /* drone thrusters */
        Enemy *e = &en[i];
        if (!e->alive || e->type != EN_DRONE || e->spawn_t > 0) continue;
        billboard(add(e->pos, V(0, -0.55f, 0)), 0.35f + 0.05f * sinf(time * 40 + i), V(0.3f, 1.2f, 3), 1, 0);
    }
    for (int s = 0; s < 4; s++) { /* gate portals shimmer */
        v3 g = add(gates[s], V(0, 3, 0));
        g = add(g, mul(norm(V(-gates[s].x, 0, -gates[s].z)), -1.7f));
        billboard(g, 2.4f + 0.2f * sinf(time * 3 + s), V(0.5f, 0.08f, 0.45f), 0.6f, 0);
    }
}

static void render_scene(float time, bool viewmodel) {
    gather_lights();
    /* shadow pass */
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_shadow);
    glViewport(0, 0, SHADOW_RES, SHADOW_RES);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(2.0f, 4.0f);
    glUseProgram(P_DEPTH);
    uD_model = U(P_DEPTH, "uModel");
    glUniformMatrix4fv(U(P_DEPTH, "uLightVP"), 1, GL_FALSE, light_vp.m);
    for (int i = 0; i < nstatic; i++)
        if (statics[i].shadow) depth_draw(statics[i].mesh, statics[i].model, NULL, 0);
    draw_dynamic(depth_draw, time);
    glDisable(GL_POLYGON_OFFSET_FILL);

    /* main HDR pass */
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_msaa);
    glViewport(0, 0, rw, rh);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glUseProgram(P_SKY);
    m4 inv = m4_inv(cam.vp);
    glUniformMatrix4fv(U(P_SKY, "uInvVP"), 1, GL_FALSE, inv.m);
    glUniform3f(U(P_SKY, "uSunDir"), SUN_DIR.x, SUN_DIR.y, SUN_DIR.z);
    glBindVertexArray(vao_empty);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    lit_begin(cam.vp, time);
    for (int i = 0; i < nstatic; i++) lit_draw(statics[i].mesh, statics[i].model, &statics[i].mat, 0);
    draw_dynamic(lit_draw, time);
    glDisable(GL_CULL_FACE);
    build_particles(time);
    flush_particles(cam.vp);

    /* first-person weapon, with its own projection so it never clips into walls */
    if (viewmodel) {
        glClear(GL_DEPTH_BUFFER_BIT);
        m4 vproj = m4_persp(54 * 3.14159f / 180, (float)rw / rh, 0.02f, 10);
        m4 vvp = m4_mul(vproj, cam.view);
        m4 inv_view = m4_inv(cam.view);
        float kick = G.recoil;
        float bobx = sinf(G.bob * 1.0f) * 0.012f, boby = fabsf(cosf(G.bob * 1.0f)) * 0.012f;
        float rel = G.reload_t > 0 ? sinf((1 - G.reload_t / 1.3f) * 3.14159f) : 0;
        v3 base = V(0.22f + bobx, -0.2f - boby - rel * 0.15f, -0.42f + kick * 0.05f);
        m4 gun = m4_mul(inv_view, m4_trs(base, 0, kick * 0.05f + rel * 0.7f, -rel * 0.3f, V(1, 1, 1)));
        glEnable(GL_CULL_FACE);
        lit_begin(vvp, time);
        static const Mat GUN = {{0.10f, 0.11f, 0.13f}, 0.9f, 0.3f, {0, 0, 0}, 0, {0, 0, 0}};
        static const Mat GUN2 = {{0.55f, 0.56f, 0.6f}, 1.0f, 0.2f, {0, 0, 0}, 0, {0, 0, 0}};
        Mat strip = MAT_GLOW_C;
        strip.emis = mul(V(0.2f, 1.6f, 3.0f), 0.3f + 0.7f * G.ammo / (float)MAG);
        Mat cell = MAT_GLOW_M;
        cell.emis = mul(V(3, 0.4f, 2.6f), G.energy >= 1 ? 1.0f : 0.2f);
        lit_draw(MS_CUBE, m4_mul(gun, m4_trs(V(0, 0, 0), 0, 0, 0, V(0.09f, 0.11f, 0.42f))), &GUN, 0);
        lit_draw(MS_CUBE, m4_mul(gun, m4_trs(V(0, 0.07f, -0.05f), 0, 0, 0, V(0.06f, 0.04f, 0.3f))), &GUN2, 0);
        lit_draw(MS_CYL8, m4_mul(gun, m4_trs(V(0, 0.01f, -0.2f), 0, 1.5708f, 0, V(0.045f, 0.32f, 0.045f))), &GUN2, 0);
        lit_draw(MS_CUBE, m4_mul(gun, m4_trs(V(0.047f, 0.02f, 0.02f), 0, 0, 0, V(0.01f, 0.025f, 0.3f))), &strip, 0);
        lit_draw(MS_CUBE, m4_mul(gun, m4_trs(V(0, -0.1f, 0.08f), 0, -0.25f, 0, V(0.06f, 0.14f, 0.07f))), &GUN, 0);
        lit_draw(MS_CYL8, m4_mul(gun, m4_trs(V(-0.05f, -0.03f, -0.02f), 0, 1.5708f, 0, V(0.05f, 0.14f, 0.05f))), &cell, 0);
        lit_draw(MS_CYL, m4_mul(gun, m4_trs(V(0, 0.01f, -0.24f), 0, 1.5708f, 0, V(0.075f, 0.13f, 0.075f))), &GUN, 0);
        lit_draw(MS_CUBE, m4_mul(gun, m4_trs(V(0, 0.105f, 0.02f), 0, 0, 0, V(0.035f, 0.035f, 0.1f))), &GUN, 0);
        lit_draw(MS_CUBE, m4_mul(gun, m4_trs(V(0, 0.105f, -0.031f), 0, 0, 0, V(0.012f, 0.012f, 0.004f))), &MAT_GLOW_O, 0);
        lit_draw(MS_CUBE, m4_mul(gun, m4_trs(V(-0.047f, 0.0f, -0.02f), 0, 0, 0, V(0.012f, 0.07f, 0.26f))), &GUN2, 0);
        lit_draw(MS_CUBE, m4_mul(gun, m4_trs(V(0, -0.03f, 0.25f), 0, 0.15f, 0, V(0.07f, 0.09f, 0.14f))), &GUN, 0);
        glDisable(GL_CULL_FACE);
        if (G.muzzle_t > 0) {
            v3 mz = m4_point(gun, V(0, 0.01f, -0.55f));
            billboard(mz, 0.014f + frand() * 0.008f, V(3, 1.8f, 0.6f), 1, 0);
            billboard(mz, 0.032f, V(0.5f, 0.28f, 0.1f), 1, 0);
            flush_particles(vvp);
        }
    }
    /* resolve, bloom, composite */
    glDisable(GL_DEPTH_TEST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_msaa);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo_hdr);
    glBlitFramebuffer(0, 0, rw, rh, 0, 0, rw, rh, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindVertexArray(vao_empty);
    glUseProgram(P_DOWN);
    glUniform1i(U(P_DOWN, "uSrc"), 0);
    glActiveTexture(GL_TEXTURE0);
    for (int i = 0; i < NMIP; i++) {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_mip[i]);
        glViewport(0, 0, mip_w[i], mip_h[i]);
        int sw = i == 0 ? rw : mip_w[i - 1], sh = i == 0 ? rh : mip_h[i - 1];
        glBindTexture(GL_TEXTURE_2D, i == 0 ? tex_hdr : tex_mip[i - 1]);
        glUniform2f(U(P_DOWN, "uTexel"), 1.0f / sw, 1.0f / sh);
        glUniform1i(U(P_DOWN, "uFirst"), i == 0);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    glUseProgram(P_UP);
    glUniform1i(U(P_UP, "uSrc"), 0);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    for (int i = NMIP - 1; i > 0; i--) {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_mip[i - 1]);
        glViewport(0, 0, mip_w[i - 1], mip_h[i - 1]);
        glBindTexture(GL_TEXTURE_2D, tex_mip[i]);
        glUniform2f(U(P_UP, "uTexel"), 1.0f / mip_w[i], 1.0f / mip_h[i]);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    glDisable(GL_BLEND);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_ldr);
    glViewport(0, 0, rw, rh);
    glUseProgram(P_COMP);
    glUniform1i(U(P_COMP, "uHdr"), 0);
    glUniform1i(U(P_COMP, "uBloom"), 1);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, tex_mip[0]);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex_hdr);
    glUniform1f(U(P_COMP, "uExposure"), 1.05f);
    glUniform1f(U(P_COMP, "uBloomAmt"), 0.16f);
    glUniform1f(U(P_COMP, "uDamage"), G.damage_fx);
    glUniform1f(U(P_COMP, "uTime"), time);
    glUniform1f(U(P_COMP, "uLowHp"), G.state == ST_DEAD ? 1.0f : clampf(1 - G.hp / 35, 0, 1));
    glUniform3f(U(P_COMP, "uSunDir"), SUN_DIR.x, SUN_DIR.y, SUN_DIR.z);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

/* ================================================================== HUD */
static float *hbuf;
static int hcount, hcap;
static void hquad(float x, float y, float w, float h, float u0, float v0, float u1, float v1, float r, float g, float b, float a) {
    if ((hcount + 6) * 8 > hcap) { hcap = hcap ? hcap * 2 : 40000; hbuf = realloc(hbuf, sizeof(float) * (size_t)hcap); }
    float P[6][4] = {{x, y, u0, v0}, {x + w, y, u1, v0}, {x + w, y + h, u1, v1}, {x, y, u0, v0}, {x + w, y + h, u1, v1}, {x, y + h, u0, v1}};
    for (int i = 0; i < 6; i++) {
        float *o = hbuf + (hcount + i) * 8;
        o[0] = P[i][0]; o[1] = P[i][1]; o[2] = P[i][2]; o[3] = P[i][3];
        o[4] = r; o[5] = g; o[6] = b; o[7] = a;
    }
    hcount += 6;
}
static void hrect(float x, float y, float w, float h, float r, float g, float b, float a) { hquad(x, y, w, h, -1, -1, -1, -1, r, g, b, a); }
/* a rotated thin rectangle (for crosshair arms and damage arcs) */
static void hline(float x0, float y0, float x1, float y1, float w, float r, float g, float b, float a) {
    float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy);
    if (l < 1e-3f) return;
    float nx = -dy / l * w * 0.5f, ny = dx / l * w * 0.5f;
    if ((hcount + 6) * 8 > hcap) { hcap = hcap ? hcap * 2 : 40000; hbuf = realloc(hbuf, sizeof(float) * (size_t)hcap); }
    float P[6][2] = {{x0 + nx, y0 + ny}, {x1 + nx, y1 + ny}, {x1 - nx, y1 - ny}, {x0 + nx, y0 + ny}, {x1 - nx, y1 - ny}, {x0 - nx, y0 - ny}};
    for (int i = 0; i < 6; i++) {
        float *o = hbuf + (hcount + i) * 8;
        o[0] = P[i][0]; o[1] = P[i][1]; o[2] = -1; o[3] = -1; o[4] = r; o[5] = g; o[6] = b; o[7] = a;
    }
    hcount += 6;
}
static float text_w(const char *s, float sc) { return (float)strlen(s) * 6 * sc - sc; }
static void htext(const char *s, float x, float y, float sc, float r, float g, float b, float a, int align) {
    if (align == 1) x -= text_w(s, sc) / 2;
    if (align == 2) x -= text_w(s, sc);
    float aw = (float)(NGLYPH * 6);
    for (; *s; s++, x += 6 * sc) {
        char c = *s >= 'a' && *s <= 'z' ? (char)(*s - 32) : *s;
        const char *at = strchr(FONT_CHARS, c);
        if (!at || c == ' ') continue;
        int gi = (int)(at - FONT_CHARS);
        float u0 = gi * 6 / aw, u1 = (gi * 6 + 5) / aw;
        hquad(x + sc * 0.6f, y + sc * 0.6f, 5 * sc, 7 * sc, u0, 0, u1, 7 / 8.0f, 0, 0, 0, a * 0.6f);
        hquad(x, y, 5 * sc, 7 * sc, u0, 0, u1, 7 / 8.0f, r, g, b, a);
    }
}
static void hud_flush(void) {
    if (!hcount) return;
    glUseProgram(P_HUD);
    glUniform2f(U(P_HUD, "uScreen"), (float)rw, (float)rh);
    glUniform1i(U(P_HUD, "uFont"), 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex_font);
    glBindVertexArray(vao_hud);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_hud);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * (size_t)hcount * 8, hbuf, GL_STREAM_DRAW);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLES, 0, hcount);
    glDisable(GL_BLEND);
    hcount = 0;
}

static void draw_bar(float x, float y, float w, float h, float v, float r, float g, float b, const char *label, float sc) {
    hrect(x - 2, y - 2, w + 4, h + 4, 0, 0, 0, 0.45f);
    hrect(x, y, w, h, r * 0.2f, g * 0.2f, b * 0.2f, 0.6f);
    hrect(x, y, w * clampf(v, 0, 1), h, r, g, b, 0.95f);
    for (float k = x + w / 10; k < x + w - 1; k += w / 10) hrect(k, y, 1, h, 0, 0, 0, 0.35f);
    htext(label, x, y - 9 * sc, sc, r, g, b, 0.9f, 0);
}

static void draw_hud(float time) {
    float s = rh / 360.0f; /* UI scale */
    float sc = fmaxf(1, floorf(s * 1.0f + 0.25f));
    float cx = rw / 2.0f, cy = rh / 2.0f;
    char b[64];
    if (G.state == ST_PLAY || G.state == ST_PAUSE) {
        /* crosshair */
        float gap = (5 + G.spread * 300 + G.recoil * 4) * s;
        float L = 6 * s, W = fmaxf(1.5f, 1.2f * s);
        for (int k = 0; k < 4; k++) {
            float dx = k == 0 ? 1 : k == 1 ? -1 : 0, dy = k == 2 ? 1 : k == 3 ? -1 : 0;
            hline(cx + dx * gap, cy + dy * gap, cx + dx * (gap + L), cy + dy * (gap + L), W + 2, 0, 0, 0, 0.5f);
            hline(cx + dx * gap, cy + dy * gap, cx + dx * (gap + L), cy + dy * (gap + L), W, 0.85f, 1, 1, 0.9f);
        }
        hrect(cx - W / 2, cy - W / 2, W, W, 1, 1, 1, 0.9f);
        if (G.hitmark > 0) {
            float a = G.hitmark / 0.15f, d0 = 6 * s, d1 = 12 * s;
            for (int k = 0; k < 4; k++) {
                float sx = k & 1 ? 1.0f : -1.0f, sy = k & 2 ? 1.0f : -1.0f;
                hline(cx + sx * d0, cy + sy * d0, cx + sx * d1, cy + sy * d1, W * 1.3f, 1, 0.35f, 0.25f, a);
            }
        }
        /* damage direction arcs */
        for (int i = 0; i < 8; i++)
            if (G.hurt_t[i] > 0) {
                float a = G.hurt_dir[i], r = 70 * s;
                for (int k = -3; k <= 3; k++) {
                    float a0 = a + k * 0.08f, a1 = a + (k + 1) * 0.08f;
                    hline(cx + sinf(a0) * r, cy - cosf(a0) * r, cx + sinf(a1) * r, cy - cosf(a1) * r, 5 * s, 1, 0.15f, 0.1f,
                          clampf(G.hurt_t[i], 0, 1) * (1 - fabsf((float)k) / 4));
                }
            }
        /* health and shield */
        float bw = 150 * s, bx = 18 * s, by = rh - 26 * s;
        draw_bar(bx, by, bw, 9 * s, G.hp / 100, G.hp < 35 ? 1 : 0.95f, G.hp < 35 ? 0.25f : 0.95f, G.hp < 35 ? 0.2f : 0.95f, "", sc);
        draw_bar(bx, by - 16 * s, bw, 6 * s, G.shield / 50, 0.3f, 0.85f, 1, "", sc);
        snprintf(b, sizeof(b), "%d", (int)ceilf(G.hp));
        htext(b, bx + bw + 8 * s, by - 2 * s, sc * 2, 1, 1, 1, 0.95f, 0);
        htext("SHIELD", bx, by - 28 * s, sc, 0.3f, 0.85f, 1, 0.8f, 0);
        /* ammo, reload and cannon cells */
        float ax = rw - 18 * s;
        if (G.reload_t > 0) {
            htext("RELOADING", ax, by - 14 * s, sc, 1, 0.8f, 0.3f, 0.9f, 2);
            hrect(ax - 80 * s, by + 2 * s, 80 * s * (1 - G.reload_t / 1.3f), 4 * s, 1, 0.8f, 0.3f, 0.9f);
        } else {
            snprintf(b, sizeof(b), "%d", G.ammo);
            htext(b, ax - 40 * s, by - 10 * s, sc * 3, G.ammo < 8 ? 1.0f : 0.9f, G.ammo < 8 ? 0.4f : 0.95f, G.ammo < 8 ? 0.3f : 1.0f, 0.95f, 2);
            snprintf(b, sizeof(b), "/%d", MAG);
            htext(b, ax, by + 2 * s, sc, 0.7f, 0.75f, 0.8f, 0.8f, 2);
        }
        for (int k = 0; k < 3; k++) {
            float f = clampf(G.energy - k, 0, 1);
            float x = ax - (3 - k) * 18 * s;
            hrect(x, by - 34 * s, 14 * s, 6 * s, 0.15f, 0.04f, 0.15f, 0.7f);
            hrect(x, by - 34 * s, 14 * s * f, 6 * s, f >= 1 ? 1.0f : 0.6f, 0.3f, f >= 1 ? 0.95f : 0.6f, 0.95f);
        }
        htext("ARC", ax - 3 * 18 * s - 4 * s, by - 34 * s, sc, 1, 0.4f, 0.9f, 0.8f, 2);
        /* wave and score */
        int left = alive_count() + G.to_spawn[0] + G.to_spawn[1] + G.to_spawn[2];
        snprintf(b, sizeof(b), "WAVE %d", G.wave);
        htext(b, cx, 14 * s, sc * 2, 1, 1, 1, 0.95f, 1);
        if (!G.between) {
            snprintf(b, sizeof(b), left == 1 ? "%d HOSTILE" : "%d HOSTILES", left);
            htext(b, cx, 32 * s, sc, 1, 0.55f, 0.35f, 0.9f, 1);
        } else {
            snprintf(b, sizeof(b), "NEXT WAVE IN %d", (int)ceilf(G.wave_t));
            htext(b, cx, 32 * s, sc, 0.4f, 0.9f, 1, 0.9f, 1);
        }
        snprintf(b, sizeof(b), "%d", G.score);
        htext(b, rw - 18 * s, 14 * s, sc * 2, 1, 1, 1, 0.95f, 2);
        if (G.combo >= 3) {
            snprintf(b, sizeof(b), "COMBO X%d", 1 + (G.combo - 1) / 3);
            htext(b, rw - 18 * s, 34 * s, sc, 1, 0.75f, 0.2f, clampf(G.combo_t, 0, 1), 2);
        }
        if (G.dash_cd > 0) hrect(cx - 15 * s, cy + 22 * s, 30 * s * (1 - G.dash_cd / 1.4f), 2 * s, 0.4f, 0.9f, 1, 0.6f);
        if (G.msg_t > 0 && G.state != ST_PAUSE) {
            float a = clampf(G.msg_t, 0, 1);
            htext(G.msg, cx, cy - 70 * s, sc * 3, 1, 0.9f, 0.6f, a, 1);
        }
        if (G.state == ST_PAUSE) {
            hrect(0, 0, rw, rh, 0, 0, 0, 0.55f);
            htext("PAUSED", cx, cy - 40 * s, sc * 4, 1, 1, 1, 1, 1);
            htext("CLICK OR ESC TO RESUME   Q TO QUIT TO TITLE", cx, cy + 10 * s, sc, 0.8f, 0.9f, 1, 1, 1);
            snprintf(b, sizeof(b), "MOUSE %.1f  (- +)    FOV %d  (, .)    INVERT Y %s  (I)", G.sens * 1000, (int)G.fov, G.invert ? "ON" : "OFF");
            htext(b, cx, cy + 30 * s, sc, 0.6f, 0.7f, 0.8f, 1, 1);
        }
    } else if (G.state == ST_TITLE) {
        float pulse = 0.85f + 0.15f * sinf(time * 2);
        htext("HARDLIGHT", cx, cy - 90 * s, sc * 7, 0.55f * pulse, 0.95f * pulse, 1, 1, 1);
        htext("ARENA DEFENSE PROTOCOL", cx, cy - 32 * s, sc * 1.5f, 1, 0.55f, 0.35f, 0.95f, 1);
        if ((int)(time * 2) % 2) htext("CLICK OR PRESS ENTER TO DEPLOY", cx, cy + 40 * s, sc * 2, 1, 1, 1, 1, 1);
        htext("WASD MOVE   MOUSE AIM   LMB FIRE   RMB ARC CANNON   SPACE JUMP   SHIFT SPRINT   Q DASH   R RELOAD",
              cx, rh - 40 * s, sc, 0.7f, 0.8f, 0.9f, 0.9f, 1);
        snprintf(b, sizeof(b), "BEST %d", G.best);
        htext(b, cx, cy + 70 * s, sc * 1.5f, 1, 0.8f, 0.3f, 0.9f, 1);
        htext("F11 FULLSCREEN   M MUTE   ESC QUIT", cx, rh - 22 * s, sc, 0.5f, 0.55f, 0.6f, 0.9f, 1);
    } else if (G.state == ST_DEAD) {
        hrect(0, 0, rw, rh, 0.1f, 0, 0, 0.35f);
        htext("SIGNAL LOST", cx, cy - 80 * s, sc * 5, 1, 0.3f, 0.25f, 1, 1);
        snprintf(b, sizeof(b), "WAVE %d   KILLS %d   ACCURACY %d%%", G.wave, G.kills, G.shots ? G.hits * 100 / G.shots : 0);
        htext(b, cx, cy - 20 * s, sc * 1.5f, 1, 1, 1, 1, 1);
        snprintf(b, sizeof(b), "SCORE %d", G.score);
        htext(b, cx, cy + 6 * s, sc * 3, 1, 0.85f, 0.4f, 1, 1);
        if (G.score >= G.best && G.score > 0) htext("NEW BEST!", cx, cy + 36 * s, sc * 2, 0.4f, 1, 0.6f, 1, 1);
        if (G.st > 1.5f && (int)(time * 2) % 2) htext("CLICK OR ENTER TO REDEPLOY   ESC FOR TITLE", cx, cy + 70 * s, sc * 1.5f, 0.8f, 0.9f, 1, 1, 1);
    }
    hud_flush();
}

/* ================================================================== frame */
static void setup_camera(v3 eye, float yaw, float pitch, float fov_deg) {
    float cp = cosf(pitch);
    v3 fwd = V(sinf(yaw) * cp, sinf(pitch), -cosf(yaw) * cp);
    cam.pos = eye;
    cam.fwd = fwd;
    cam.view = m4_look(eye, add(eye, fwd), V(0, 1, 0));
    cam.proj = m4_persp(fov_deg * 3.14159f / 180, (float)rw / rh, 0.08f, 400);
    cam.vp = m4_mul(cam.proj, cam.view);
    cam.right = norm(cross(fwd, V(0, 1, 0)));
    cam.up = cross(cam.right, fwd);
    listener_pos = eye;
    listener_right = cam.right;
}

static void render_frame(float time) {
    if (G.state == ST_TITLE) {
        float a = time * 0.12f;
        v3 eye = V(sinf(a) * 24, 9 + sinf(time * 0.3f) * 2, cosf(a) * 24);
        v3 d = norm(sub(V(0, 2, 0), eye));
        setup_camera(eye, atan2f(d.x, -d.z), asinf(d.y), 70);
        render_scene(time, false);
    } else {
        float sh = G.shake * G.shake;
        float bobv = G.ground ? sinf(G.bob * 2) * 0.04f : 0;
        v3 eye = add(G.pos, V(frs() * sh * 0.15f, 1.6f + bobv + frs() * sh * 0.15f, frs() * sh * 0.15f));
        if (G.state == ST_DEAD) eye.y = G.pos.y + 0.4f + 1.2f * expf(-G.st * 3);
        float roll_fov = G.fov + (G.dash_t > 0 ? 8 : 0) + (G.state == ST_PLAY && G.vel.x * G.vel.x + G.vel.z * G.vel.z > 70 ? 4 : 0);
        setup_camera(eye, G.yaw + frs() * sh * 0.01f, G.pitch + frs() * sh * 0.01f, roll_fov);
        render_scene(time, G.state != ST_DEAD);
    }
    draw_hud(time);
}

/* ================================================================== self-test */
static void save_bmp(const char *path) {
    unsigned char *px = malloc((size_t)rw * rh * 4);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_ldr);
    glReadPixels(0, 0, rw, rh, GL_BGRA, GL_UNSIGNED_BYTE, px);
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormatFrom(px, rw, rh, 32, rw * 4, SDL_PIXELFORMAT_ARGB8888);
    SDL_Surface *f = SDL_CreateRGBSurfaceWithFormat(0, rw, rh, 32, SDL_PIXELFORMAT_ARGB8888);
    if (s && f) {
        for (int y = 0; y < rh; y++) memcpy((Uint8 *)f->pixels + y * f->pitch, px + (size_t)(rh - 1 - y) * rw * 4, (size_t)rw * 4);
        SDL_SaveBMP(f, path);
    }
    SDL_FreeSurface(s);
    SDL_FreeSurface(f);
    free(px);
}

static Input bot_input(float dt, float t) {
    Input in = {0};
    v3 eye = add(G.pos, V(0, 1.6f, 0));
    Enemy *tgt = NULL;
    float best = 1e9f;
    for (int i = 0; i < MAXEN; i++) {
        if (!en[i].alive || en[i].spawn_t > 0) continue;
        float d = len(sub(en_center(&en[i]), eye));
        if (en[i].type == EN_CRAWLER) d *= 0.6f; /* crawlers first: they hurt up close */
        if (d < best && los(eye, en_center(&en[i]))) { best = d; tgt = &en[i]; }
    }
    static float strafe_t, strafe = 1;
    strafe_t -= dt;
    if (strafe_t <= 0) { strafe = -strafe; strafe_t = 1.2f + frand() * 1.5f; }
    in.side = strafe;
    if (tgt) {
        v3 aim = sub(en_center(tgt), eye);
        float want_yaw = atan2f(aim.x, -aim.z), want_pitch = atan2f(aim.y, sqrtf(aim.x * aim.x + aim.z * aim.z));
        float dy = remainderf(want_yaw - G.yaw, 6.2832f), dp = want_pitch - G.pitch;
        static Enemy *last;
        static float react;
        if (tgt != last) { last = tgt; react = 0.25f + frand() * 0.2f; } /* human reaction time */
        react -= dt;
        float rate = 4.5f * dt; /* a human-ish turn speed, with wobble */
        float wob = sinf(t * 7.3f) * 0.035f + sinf(t * 3.1f) * 0.025f;
        in.turn_x = clampf(dy + wob, -rate, rate) * 0.8f;
        in.turn_y = clampf(dp + wob * 0.6f, -rate, rate) * 0.8f;
        float err = sqrtf(dy * dy + dp * dp);
        in.fire = react <= 0 && err < 0.09f;
        if (tgt->type == EN_SENTINEL && err < 0.05f) in.alt = true;
        if (tgt->type == EN_CRAWLER && best < 4) { in.fwd = -1; if (frand() < 0.02f) in.dash = true; }
    } else {
        float dy = remainderf(t * 0.6f - G.yaw, 6.2832f);
        in.turn_x = clampf(dy, -3 * dt, 3 * dt);
        in.turn_y = clampf(-G.pitch, -dt, dt);
        if (G.ammo < MAG / 2) in.reload = true;
    }
    float r = sqrtf(G.pos.x * G.pos.x + G.pos.z * G.pos.z);
    if (r > 16) { /* drift back toward the middle */
        float a = atan2f(-G.pos.x, G.pos.z) - G.yaw;
        in.fwd += -cosf(a) * 0.8f;
        in.side += sinf(a) * 0.8f;
    }
    in.sprint = true;
    in.jump = frand() < 0.008f;
    return in;
}

static int run_selftest(const char *dir, SDL_Window *win) {
    char path[1200];
    int fails = 0;
    (void)win;
    printf("GL: %s | %s\n", glGetString(GL_VERSION), glGetString(GL_RENDERER));
    printf("shaders: %s\n", shader_errors ? "ERRORS" : "all compiled and linked");
    if (shader_errors) fails++;
#define SHOT(name) do { snprintf(path, sizeof(path), "%s/%s.bmp", dir, name); save_bmp(path); } while (0)
    G.state = ST_TITLE;
    render_frame(3.0f);
    SHOT("title");
    /* timing on the GPU */
    new_game();
    glFinish();
    Uint64 t0 = SDL_GetPerformanceCounter();
    for (int i = 0; i < 120; i++) { G.yaw = i * 0.05f; render_frame(i / 60.0f); }
    glFinish();
    double ms = (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / SDL_GetPerformanceFrequency() / 120;
    printf("render: %.2f ms per frame at %dx%d (4x MSAA, shadows, bloom)\n", ms, rw, rh);

    /* the bot fights for real (no invulnerability) */
    srand(1234);
    new_game();
    float t = 0, dt = 1 / 60.0f;
    int shots_taken = 0, frame = 0, max_wave = 1;
    bool took_combat = false, took_boom = false, took_sentinel = false, took_hurt = false;
    G.dmg_taken = 0;
    while (G.state == ST_PLAY && t < 60 * 20 && G.wave <= 20) {
        Input in = bot_input(dt, t);
        update_player(&in, dt);
        update_enemies(dt);
        update_world(dt);
        update_waves(dt);
        G.damage_fx = fmaxf(0, G.damage_fx - dt * 1.5f);
        G.shake = fmaxf(0, G.shake - dt * 2.5f);
        G.recoil = fmaxf(0, G.recoil - dt * 6);
        G.hitmark -= dt;
        G.msg_t -= dt;
        G.muzzle_t -= dt;
        for (int i = 0; i < 8; i++) G.hurt_t[i] -= dt;
        t += dt;
        frame++;
        if (G.wave > max_wave) max_wave = G.wave;
        bool render_now = frame % 30 == 0;
        int nalive = alive_count();
        if (!took_combat && G.wave == 2 && nalive >= 5 && G.muzzle_t > 0) { render_now = true; took_combat = true; render_frame(t); SHOT("combat"); shots_taken++; }
        if (!took_boom) {
            for (int i = 0; i < MAXDL; i++)
                if (dl[i].life > dl[i].max * 0.6f && dl[i].radius > 12 && len(sub(dl[i].pos, G.pos)) < 22) {
                    render_frame(t); SHOT("explosion"); took_boom = true; shots_taken++; break;
                }
        }
        if (!took_sentinel)
            for (int i = 0; i < MAXEN; i++)
                if (en[i].alive && en[i].type == EN_SENTINEL && en[i].spawn_t <= 0 && G.muzzle_t > 0) {
                    v3 d = sub(en[i].pos, G.pos);
                    float a = remainderf(atan2f(d.x, -d.z) - G.yaw, 6.2832f);
                    if (fabsf(a) < 0.3f) { render_frame(t); SHOT("sentinel"); took_sentinel = true; shots_taken++; }
                    break;
                }
        if (G.wave == 3 && G.between == false && G.st >= 0 && frame % 600 == 0) { int st = G.state; G.state = ST_PAUSE; render_frame(t); SHOT("pause"); G.state = st; }
        if (!took_hurt && G.damage_fx > 0.6f) { render_frame(t); SHOT("hurt"); took_hurt = true; shots_taken++; }
        if (render_now) render_frame(t);
    }
    printf("bot: reached wave %d, %d kills, accuracy %d%%, %s after %.1f min (hp %.0f, %.0f damage taken)\n", max_wave, G.kills,
           G.shots ? G.hits * 100 / G.shots : 0, G.state == ST_DEAD ? "died" : "still alive", t / 60, G.hp, G.dmg_taken);
    if (max_wave < 3) { printf("FAIL: the bot should get past wave 2\n"); fails++; }
    if (G.state != ST_DEAD) { G.hp = 1; damage_player(100, add(G.pos, V(3, 1, 0))); }
    G.st = 2;
    render_frame(t + 0.5f);
    SHOT("dead");
    /* deliberately starve the player: a standing target must lose (proves enemies can hurt) */
    srand(99);
    new_game();
    for (float tt = 0; tt < 120 && G.state == ST_PLAY; tt += dt) {
        Input none = {0};
        update_player(&none, dt);
        update_enemies(dt);
        update_world(dt);
        update_waves(dt);
    }
    printf("idle player: %s\n", G.state == ST_DEAD ? "died as expected" : "SURVIVED (enemies too weak?)");
    if (G.state != ST_DEAD) fails++;
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) { printf("FAIL: GL error 0x%x\n", err); fails++; }
    printf("screenshots: %d action shots + title/pause/dead\n", shots_taken);
    printf("selftest: %s\n", fails ? "FAILED" : "all checks passed");
    return fails ? 1 : 0;
}

/* ================================================================== main */
int main(int argc, char **argv) {
    const char *shotdir = NULL;
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--selftest") && i + 1 < argc) shotdir = argv[++i];
    selftest = shotdir != NULL;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_Window *win = SDL_CreateWindow("Hardlight", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720,
                                       SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | (selftest ? SDL_WINDOW_HIDDEN : 0));
    if (!win) { fprintf(stderr, "window: %s\n", SDL_GetError()); return 1; }
    SDL_GLContext ctx = SDL_GL_CreateContext(win);
    if (!ctx) { fprintf(stderr, "OpenGL 3.3 context: %s\n", SDL_GetError()); return 1; }
    SDL_GL_SetSwapInterval(selftest ? 0 : -1) ;
    if (!selftest && SDL_GL_GetSwapInterval() == 0) SDL_GL_SetSwapInterval(1);
    init_gl();
    build_arena();
    int dw, dh;
    SDL_GL_GetDrawableSize(win, &dw, &dh);
    if (selftest) { dw = 1280; dh = 720; }
    make_targets(dw, dh);
    gen_sounds();
    G.fov = 90;
    G.sens = 0.0022f;
    G.best = 0;
    char *pref = SDL_GetPrefPath("doomedl", "hardlight");
    if (pref) {
        snprintf(save_path, sizeof(save_path), "%sbest.txt", pref);
        SDL_free(pref);
        FILE *f = fopen(save_path, "r");
        if (f) {
            int inv = 0;
            if (fscanf(f, "%d %f %f %d", &G.best, &G.sens, &G.fov, &inv) >= 1) G.invert = inv;
            fclose(f);
            G.sens = clampf(G.sens, 0.0004f, 0.01f);
            G.fov = clampf(G.fov, 70, 110);
        }
    }
    if (selftest) {
        int rc = run_selftest(shotdir, win);
        SDL_Quit();
        return rc;
    }
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) == 0) {
        SDL_AudioSpec want = {0}, have;
        want.freq = RATE;
        want.format = AUDIO_F32SYS;
        want.channels = 2;
        want.samples = 512;
        want.callback = audio_cb;
        adev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
        if (adev) { SDL_PauseAudioDevice(adev, 0); start_music(); }
    }
    SDL_GameController *pad = NULL;
    for (int i = 0; i < SDL_NumJoysticks() && !pad; i++)
        if (SDL_IsGameController(i)) pad = SDL_GameControllerOpen(i);

    G.state = ST_TITLE;
    Uint64 last = SDL_GetPerformanceCounter(), freq = SDL_GetPerformanceFrequency();
    float time = 0;
    bool running = true;
    float mdx = 0, mdy = 0;
    bool click = false;
    while (running) {
        SDL_Event e;
        click = false;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_QUIT: running = false; break;
            case SDL_WINDOWEVENT:
                if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                    SDL_GL_GetDrawableSize(win, &dw, &dh);
                    if (dw > 0 && dh > 0) make_targets(dw, dh);
                }
                if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST && G.state == ST_PLAY) {
                    G.state = ST_PAUSE;
                    SDL_SetRelativeMouseMode(SDL_FALSE);
                }
                break;
            case SDL_MOUSEMOTION:
                if (G.state == ST_PLAY) { mdx += e.motion.xrel; mdy += e.motion.yrel; }
                break;
            case SDL_MOUSEBUTTONDOWN: click = true; break;
            case SDL_CONTROLLERDEVICEADDED: if (!pad) pad = SDL_GameControllerOpen(e.cdevice.which); break;
            case SDL_KEYDOWN: {
                if (e.key.repeat) break;
                SDL_Keycode k = e.key.keysym.sym;
                if (k == SDLK_F11) {
                    bool fs = SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN_DESKTOP;
                    SDL_SetWindowFullscreen(win, fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
                } else if (k == SDLK_m) {
                    muted = !muted;
                } else if (G.state == ST_TITLE) {
                    if (k == SDLK_ESCAPE) running = false;
                    else if (k == SDLK_RETURN || k == SDLK_SPACE) click = true;
                } else if (G.state == ST_PLAY) {
                    if (k == SDLK_ESCAPE || k == SDLK_p) { G.state = ST_PAUSE; SDL_SetRelativeMouseMode(SDL_FALSE); }
                } else if (G.state == ST_PAUSE) {
                    if (k == SDLK_ESCAPE || k == SDLK_p) { G.state = ST_PLAY; SDL_SetRelativeMouseMode(SDL_TRUE); }
                    else if (k == SDLK_q) G.state = ST_TITLE;
                    else if (k == SDLK_MINUS) G.sens = fmaxf(0.0004f, G.sens - 0.0002f);
                    else if (k == SDLK_EQUALS || k == SDLK_PLUS) G.sens = fminf(0.01f, G.sens + 0.0002f);
                    else if (k == SDLK_COMMA) G.fov = fmaxf(70, G.fov - 5);
                    else if (k == SDLK_PERIOD) G.fov = fminf(110, G.fov + 5);
                    else if (k == SDLK_i) G.invert = !G.invert;
                } else if (G.state == ST_DEAD) {
                    if (k == SDLK_ESCAPE) G.state = ST_TITLE;
                    else if ((k == SDLK_RETURN || k == SDLK_SPACE) && G.st > 1.5f) click = true;
                }
                break;
            }
            }
        }
        Uint64 now = SDL_GetPerformanceCounter();
        float dt = (float)(now - last) / freq;
        last = now;
        if (dt > 1 / 30.0f) dt = 1 / 30.0f;
        time += dt;
        G.st += dt;

        if (click) {
            if (G.state == ST_TITLE || (G.state == ST_DEAD && G.st > 1.5f)) { new_game(); SDL_SetRelativeMouseMode(SDL_TRUE); mdx = mdy = 0; }
            else if (G.state == ST_PAUSE) { G.state = ST_PLAY; SDL_SetRelativeMouseMode(SDL_TRUE); mdx = mdy = 0; }
        }
        if (G.state == ST_PLAY) {
            const Uint8 *k = SDL_GetKeyboardState(NULL);
            Uint32 mb = SDL_GetMouseState(NULL, NULL);
            Input in = {0};
            in.fwd = (float)(k[SDL_SCANCODE_W] || k[SDL_SCANCODE_UP]) - (float)(k[SDL_SCANCODE_S] || k[SDL_SCANCODE_DOWN]);
            in.side = (float)(k[SDL_SCANCODE_D] || k[SDL_SCANCODE_RIGHT]) - (float)(k[SDL_SCANCODE_A] || k[SDL_SCANCODE_LEFT]);
            in.turn_x = mdx * G.sens * (G.fov / 90);
            in.turn_y = -mdy * G.sens * (G.fov / 90) * (G.invert ? -1 : 1);
            mdx = mdy = 0;
            in.jump = k[SDL_SCANCODE_SPACE];
            in.sprint = k[SDL_SCANCODE_LSHIFT] || k[SDL_SCANCODE_RSHIFT];
            in.fire = (mb & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0 && !click;
            in.alt = (mb & SDL_BUTTON(SDL_BUTTON_RIGHT)) != 0;
            in.dash = k[SDL_SCANCODE_Q] || k[SDL_SCANCODE_LCTRL];
            in.reload = k[SDL_SCANCODE_R];
            if (pad) {
                float lx = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX) / 32767.0f;
                float ly = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY) / 32767.0f;
                float rx = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTX) / 32767.0f;
                float ry = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTY) / 32767.0f;
                if (fabsf(lx) > 0.15f) in.side += lx;
                if (fabsf(ly) > 0.15f) in.fwd -= ly;
                if (fabsf(rx) > 0.12f) in.turn_x += rx * fabsf(rx) * 3.2f * dt;
                if (fabsf(ry) > 0.12f) in.turn_y -= ry * fabsf(ry) * 2.4f * dt * (G.invert ? -1 : 1);
                in.fire |= SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 12000;
                in.alt |= SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 12000;
                in.jump |= SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_A);
                in.dash |= SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_B);
                in.reload |= SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_X);
                in.sprint |= SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_LEFTSTICK);
                if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_START)) { G.state = ST_PAUSE; SDL_SetRelativeMouseMode(SDL_FALSE); }
            }
            update_player(&in, dt);
            update_enemies(dt);
            update_world(dt);
            update_waves(dt);
        } else if (G.state == ST_TITLE || G.state == ST_DEAD) {
            update_enemies(dt);
            update_world(dt);
            if (pad && SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_START) && (G.state == ST_TITLE || G.st > 1.5f)) {
                new_game();
            }
        }
        G.damage_fx = fmaxf(0, G.damage_fx - dt * 1.5f);
        G.shake = fmaxf(0, G.shake - dt * 2.5f);
        G.recoil = fmaxf(0, G.recoil - dt * 6);
        G.hitmark -= dt;
        G.msg_t -= dt;
        G.muzzle_t -= dt;
        for (int i = 0; i < 8; i++) G.hurt_t[i] -= dt;

        render_frame(time);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_ldr);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBlitFramebuffer(0, 0, rw, rh, 0, 0, rw, rh, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        SDL_GL_SwapWindow(win);
    }
    if (save_path[0]) {
        FILE *f = fopen(save_path, "w");
        if (f) { fprintf(f, "%d %.5f %.1f %d\n", G.best, G.sens, G.fov, G.invert); fclose(f); }
    }
    if (adev) SDL_CloseAudioDevice(adev);
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
