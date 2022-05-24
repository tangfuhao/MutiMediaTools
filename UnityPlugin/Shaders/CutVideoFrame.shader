Shader "AvatarWorks/CutVideoFrame"
{
    Properties
    {
        _MainTex ("Texture", 2D) = "white" {}
        _UVX_OFFSET("uv_x_offset",Float) = 0
        _UVY_OFFSET("uv_y_offset",Float) = 0
        _UVX_SCALE("uv_x_scale",Float) = 0
        _UVy_SCALE("uv_y_scale",Float) = 0
        
    }
    SubShader
    {
        // No culling or depth
        Cull Off ZWrite Off ZTest Off

        Pass
        {
            CGPROGRAM
            #pragma vertex vert
            #pragma fragment frag

            #include "UnityCG.cginc"

            struct appdata
            {
                float4 vertex : POSITION;
                float2 uv : TEXCOORD0;
            };

            struct v2f
            {
                float2 uv : TEXCOORD0;
                float4 vertex : SV_POSITION;
            };

            float _UVX_OFFSET;
            float _UVY_OFFSET;
            float _UVX_SCALE;
            float _UVY_SCALE;

            v2f vert (appdata v)
            {
                v2f o;
                o.vertex = UnityObjectToClipPos(v.vertex);
                o.uv.x = v.uv.x * _UVX_SCALE + _UVX_OFFSET;
                o.uv.y = v.uv.y * _UVY_SCALE + _UVY_OFFSET;
                return o;
            }

            sampler2D _MainTex;

            fixed4 frag (v2f i) : SV_Target
            {
                return tex2D(_MainTex, i.uv);
            }
            ENDCG
        }
    }
}
