uniform sampler2D edgeTex;
uniform sampler2D colorTex;
uniform sampler2D offsetTex;
uniform sampler2D videoTex;

in vec2 leftTop;
in vec2 edgePos;
in vec4 misc;
in vec2 videoCoord;

out vec4 fragColor;

void main()
{
	// 12-bit edge information encoded as 64x64 texture-coordinate
	vec2 xy = texture(edgeTex, edgePos).ra;

	// extend to (64N x 64N) texture-coordinate
	vec2 subPixelPos = misc.xy;
	xy = (floor(64.0 * xy) + fract(subPixelPos)) / 64.0;
	vec2 offset = texture(offsetTex, xy).xw;

	// fract not really needed, but it eliminates one MOV instruction
	vec2 texStep2 = fract(misc.zw);
	vec4 col = texture(colorTex, leftTop + offset * texStep2);

#if SUPERIMPOSE
	vec4 vid = texture(videoTex, videoCoord);
	fragColor = mix(vid, col, col.a);
#else
	fragColor = col;
#endif
}
