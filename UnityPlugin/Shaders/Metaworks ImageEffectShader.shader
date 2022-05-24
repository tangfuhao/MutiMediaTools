Shader "Metaworks/ImageEffectShader"
{
    Properties
    {
        _MainTex ("Texture", 2D) = "white" {}
        _RotationRadian("RotationRadian",float) = 0
    }
    SubShader
    {
        // No culling or depth
        Cull Off ZWrite Off ZTest Always

        Pass
        {
            CGPROGRAM
            #pragma vertex vert
            #pragma fragment frag

            #include "UnityCG.cginc"

            float _RotationRadian;

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

            v2f vert (appdata v)
            {
                const float PI = 3.14159265;
                v2f o;
                o.vertex = UnityObjectToClipPos(v.vertex);
                // o.uv = v.uv;
                // o.uv.x = v.uv.x;
                // o.uv.y = (1 - v.uv.y);

                float s = sin ( _RotationRadian );
                float c = cos (_RotationRadian );
            
                float2x2 rotationMatrix = float2x2( c, -s, s, c);
                o.uv = mul(v.uv.xy - 0.5, rotationMatrix) + 0.5;
                o.uv.y = (1 - o.uv.y);
                

            //    o.uv = TRANSFORM_TEX (v.uv, _MainTex);
                return o;
            }

            sampler2D _MainTex;

            fixed4 frag (v2f i) : SV_Target
            {
                fixed4 col = tex2D(_MainTex, i.uv);
                return col;
            }
            ENDCG
        }
    }
}
