// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef RASTER_DIV
#define RASTER_DIV(n,d) ((n)/(d))
#endif
uint loadTexel(uint address){
#ifdef NATIVE_SCENE
 if(address<frame.colors.z)return texelFetch(packedTexels,int(address)).r;
#endif
 return source[address];
}
uvec4 unpack(uint c){return uvec4(c,c>>8,c>>16,c>>24)&255u;}
uint pack(uvec4 c){return c.x|(c.y<<8)|(c.z<<16)|(c.w<<24);}
float divide(float a,float b);
uint maskAlpha(uint sourceAlpha,uint blue,uint amount,uint mode){
 if(amount==255u)return sourceAlpha;
 uint power=1u<<min(mode&15u,7u),span=RASTER_DIV(256u,power);
 int threshold=(255-int(amount))*int(span+255u)/255-int(span);
 int value=(mode&128u)!=0u?255-int(blue):int(blue);
 int wrapped=((value-threshold)*int(power))&65535;if(wrapped>=32768)wrapped-=65536;
 uint coverage=uint(clamp(wrapped,0,255));return sourceAlpha*((coverage>>1)+(coverage>>7))>>7;
}
uvec4 pixel(Draw p,ivec2 q){
 q=clamp(q,ivec2(0),ivec2(p.meta.xy)-1);
 if((p.meta.z&262144u)==0u)return unpack(loadTexel(p.colors.z+uint(q.y)*p.meta.x+uint(q.x)));
 precise vec2 uv=vec2(divide(float(q.x)+.5,float(p.meta.x)),divide(float(q.y)+.5,float(p.meta.y)));
 precise vec2 from=p.maskSource.xy+uv*p.maskSource.zw,mask=p.maskRect.xy+uv*p.maskRect.zw;
 ivec2 sq=clamp(ivec2(floor(from)),ivec2(0),ivec2(p.meta.xy)-1),mq=clamp(ivec2(floor(mask)),ivec2(0),ivec2(p.maskMeta.xy)-1);
 uvec4 color=unpack(loadTexel(p.colors.z+uint(sq.y)*p.meta.x+uint(sq.x)));
 uint blue=loadTexel(p.maskMeta.z+uint(mq.y)*p.maskMeta.x+uint(mq.x))&255u;
 color.a=maskAlpha(color.a,blue,p.maskMeta.w&255u,p.maskMeta.w>>8);return color;
}
uvec4 samplePixel(Draw p,vec2 uv){
 if((p.meta.z&255u)!=2u)return pixel(p,ivec2(floor(uv)));
 precise vec2 f=uv-vec2(.5);ivec2 i=ivec2(floor(f));f-=vec2(i);
 precise vec4 a=vec4(pixel(p,i)),b=vec4(pixel(p,i+ivec2(1,0))),c=vec4(pixel(p,i+ivec2(0,1))),d=vec4(pixel(p,i+ivec2(1,1)));
 precise vec4 value=(a*(1-f.x)+b*f.x)*(1-f.y)+(c*(1-f.x)+d*f.x)*f.y;
 return uvec4(floor(value+.5));
}
int shift7(int n){return n>=0?n/128:-((-n+127)/128);}
uint blend(uint mode,uint source,uint dest,uint alpha){
 int s=int(source),d=int(dest),a=int(alpha),v=0,w=s*(a+(a>>7))/256;
 if(mode<2u)v=d+shift7((s-d)*((a+(a>>7))>>1));
 else if(mode==2u)v=w+d-w*d/256;
 else if(mode==3u||mode==6u){int f=(a>>1)+((a>>1)>>6);int x=255+shift7((s-255)*f);v=mode==3u?(x+(x>>7))*d/256:min(x,d);}
 else if(mode==4u)v=abs(w-d);else if(mode==5u)v=max(w,d);else if(mode==7u)v=w+d;else if(mode==8u)v=d-w;
 return uint(clamp(v,0,255));
}
float divide(float a,float b){precise float z=a/b;precise float residual=fma(-z,b,a);precise float result=z+residual/b;return result;}

uvec4 tint(Draw p,uvec4 s){
 s.a=s.a*p.meta.w/255u;
 if(p.colors.x!=0u||p.colors.y!=0xffffffu)s.rgb=min(uvec3(255),s.rgb*unpack(p.colors.y).rgb/255u+unpack(p.colors.x).rgb);
 return s;
}
uvec4 composite(uint mode,uvec4 s,uvec4 d){
 uint a=s.a;
 if(mode<2u){
  if(a==255u)return s;
  return uvec4(ivec4(d)+((ivec4(s)-ivec4(d))*int((a+(a>>7))>>1)>>7));
 }
 return uvec4(blend(mode,s.r,d.r,a),blend(mode,s.g,d.g,a),blend(mode,s.b,d.b,a),blend(mode,a,d.a,a));
}
uvec4 glyphOver(uvec4 s,uvec4 d){
 uint a=s.a+d.a*(255u-s.a)/255u;
 return uvec4(RASTER_DIV(s.rgb*s.a+d.rgb*d.a*(255u-s.a)/255u,a),a);
}
uint ruleAlpha(Draw p,uvec2 q){
 if(p.meta.w==255u)return 255u;
 uint at=p.colors.w+(q.y*p.meta.y/720u)*p.meta.x+q.x*p.meta.x/1280u;
 return maskAlpha(255u,loadTexel(at)&255u,p.meta.w,p.colors.y);
}
uint fadeByte(uint base,uint pair){
 uint word=loadTexel(base+(pair>>2));
 return (word>>(8u*(pair&3u)))&255u;
}
uvec4 fadePixel(Draw p,uvec4 old,uvec4 d,uint alpha){
 if((p.meta.z&131072u)==0u){
  uvec4 pair=old*256u+d;
  return uvec4(fadeByte(p.colors.w,pair.r),fadeByte(p.colors.w,pair.g),fadeByte(p.colors.w,pair.b),fadeByte(p.colors.w,pair.a));
 }
 return uvec4(ivec4(d)+((ivec4(old)-ivec4(d))*int((alpha+(alpha>>7))>>1)>>7));
}
