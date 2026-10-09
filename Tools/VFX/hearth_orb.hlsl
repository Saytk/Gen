// M_Curffe_HearthOrb : flammes du Foyer de Curffe en orbes de feu (avis utilisateur du 2026-10-09 : « pas des orbes de
// flamme avec de la puissance »). Même famille que M_Curffe_FlameComet : tête ronde + langues pointues en 4 couches
// plates (rouge, orange, jaune, cœur crème), jamais blanc, pas de bord sombre ; ici les langues montent (carte orientée
// vers la caméra par le C++, haut = +Z monde). Cœur HDR qui pulse (le bloom fait le halo), braises qui montent.
// Carte 1:2 (largeur:hauteur). Repère : b en travers (-1..1), a vers le haut depuis le centre de l'orbe, en demi-largeurs.
// Entrées : UV, T (temps), Rnd (aléa d'instance), Tex (bruit), Lit, Pop (données d'instance 0 et 1), Intensity, EmberR,
// EmberColour, Speed.
const float OrbV = 0.76;   // centre de l'orbe, en V de la carte (0 = haut)
const float R = 0.78;      // rayon de la tête
const float len = 2.05;    // longueur des langues au-dessus du centre
float Lf = saturate(Lit);
float P = saturate(Pop);
float s = max(smoothstep(0.0, 1.0, Lf) * (1.0 + 0.22 * P), 0.001);
float T2 = T * Speed + Rnd * 23.0;
float b0 = (UV.x - 0.5) * 2.0;
float a0 = (OrbV - UV.y) * 4.0;
float b = b0 / s;
float a = a0 / s;
float t = a / len;
// bruit qui monte : bords organiques sur les langues, nul sur la tête
float n = Texture2DSample(Tex, TexSampler, float2(UV.x * 0.8 + Rnd, UV.y * 0.5 + T2 * 0.9)).r;
b += (n - 0.5) * 0.10 * saturate(t * 2.5);
// les voies se resserrent vers le haut : la flamme se termine en pointe
float G = 1.0 + 0.12 * smoothstep(0.0, 0.2, t) - 0.50 * saturate(t);
float aa = max(fwidth(b), fwidth(a)) * 1.2 + 0.004;
// le cœur respire : la puissance se voit même à l'arrêt
float pulse = 1.0 + 0.07 * sin(T2 * 5.0);

float rs[4] = { 1.0, 0.80, 0.58, 0.38 };
// langues : position latérale (voie), demi-largeur, longueur relative ; couches 0..3 = 5 + 4 + 3 + 1
float3 lanes[13] = {
    float3(-0.70, 0.36, 0.60), float3(-0.34, 0.42, 0.86), float3(0.00, 0.46, 1.00), float3(0.36, 0.42, 0.80), float3(0.72, 0.36, 0.56),
    float3(-0.54, 0.40, 0.52), float3(-0.18, 0.44, 0.76), float3(0.20, 0.44, 0.70), float3(0.56, 0.38, 0.48),
    float3(-0.28, 0.40, 0.46), float3(0.04, 0.46, 0.60), float3(0.32, 0.38, 0.40),
    float3(0.00, 0.38, 0.26) };
float dk[4];
for (int k = 0; k < 4; k++) {
    float Rk = R * rs[k] * (k == 3 ? pulse : 1.0);
    dk[k] = length(float2(b, a + (R - Rk) * 0.45)) - Rk;
}
for (int j = 0; j < 13; j++) {
    int k = j < 5 ? 0 : (j < 9 ? 1 : (j < 12 ? 2 : 3));
    float3 L = lanes[j];
    float Lj = L.z * (0.82 + 0.18 * sin(T2 * (5.0 + j * 1.3) + j * 2.1));
    float wob = 0.09 * sin(t * 8.0 - T2 * 9.0 + j * 1.7) * saturate(t * 2.0);
    float hw = L.y * smoothstep(-0.05, 0.16, t) * pow(saturate(1.0 - t / Lj), 0.6);
    float Rt = R * rs[k] * G;
    float dj = (abs(b / Rt - L.x - wob) - hw) * Rt;
    dk[k] = min(dk[k], (t > -0.1 && t < Lj) ? dj : 1.0);
}
// braises : trois points qui montent au-dessus des pointes et s'éteignent
float de = 1.0;
for (int i = 0; i < 3; i++) {
    float ph = frac(T2 * 0.55 + i / 3.0);
    float2 ep = float2(sin(i * 2.4 + Rnd * 6.2832) * 0.45 + 0.12 * sin(T2 * 3.0 + i), len * 0.55 + ph * 1.1);
    de = min(de, length(float2(b, a) - ep) - 0.075 * (1.0 - ph));
}
// couleurs (émissif) : dégradé chaud, chaque couche s'assombrit un peu vers sa pointe
float shade = lerp(1.06, 0.78, saturate(t));
float3 cRed = float3(1.10, 0.10, 0.015);
float3 cOrange = float3(1.45, 0.42, 0.035);
float3 cYellow = float3(1.60, 0.85, 0.120);
float3 cCream = float3(1.60, 1.15, 0.420);
float m0 = 1.0 - smoothstep(-aa, aa, dk[0]);
float m1 = 1.0 - smoothstep(-aa, aa, dk[1]);
float m2 = 1.0 - smoothstep(-aa, aa, dk[2]);
float m3 = 1.0 - smoothstep(-aa, aa, dk[3]);
float me = 1.0 - smoothstep(-aa, aa, de);
float3 col = cRed * shade;
col = lerp(col, cOrange * shade, m1);
col = lerp(col, cYellow * shade, m2);
float rc = saturate(length(float2(b, a + R * 0.62 * 0.45)) / (R * 0.4));
col = lerp(col, lerp(cCream * 1.35, cYellow, rc * rc), m3);
col *= 1.0 + (n - 0.5) * 0.16 * saturate(t * 4.0);
col = lerp(col, cOrange * 1.2, me * (1.0 - m0));
col *= Intensity * (1.0 + 0.6 * P);
float flame = max(m0, me) * step(0.02, Lf);
// emplacement vide : braise sombre ronde (lisible « 3 sur 5 »)
float ember = (length(float2(b0, a0)) < EmberR && Lf < 0.5) ? 1.0 : 0.0;
float3 C = flame > 0.5 ? col : EmberColour.rgb;
return float4(C, max(flame, ember));
