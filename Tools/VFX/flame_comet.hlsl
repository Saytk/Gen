// M_Curffe_FlameComet : grosse boule de feu façon comète cartoon (référence utilisateur du 2026-10-09).
// Tête ronde + langues de flamme pointues en 4 couches (rouge, orange, jaune, cœur crème) qui ondulent vers l'arrière.
// Sprite aligné sur la vitesse, LR = longueur / largeur (taille du sprite et pivot c / (2 LR) à garder en phase). Repère : b en travers (-1..1), y le long de l'axe
// (0 = avant), en demi-largeurs. Entrées : UV, T (temps), Rand (aléa particule), Tex (bruit), Tint, Alpha, Intensity.
const float LR = 2.6;
const float FLIP = 0.0;  // 1 si l'avant du sprite aligné sur la vitesse est en bas de la texture
float b = (UV.x - 0.5) * 2.0;
float y = (FLIP > 0.5 ? 1.0 - UV.y : UV.y) * 2.0 * LR;
float R = 0.68;
float c = 1.05;
float len = 2.0 * LR - c - 0.1;
float t = (y - c) / len;
// bruit qui file vers l'arrière : bords organiques, nul sur la tête
float n = Texture2DSample(Tex, TexSampler, float2(UV.x * 0.7 + Rand, y * 0.22 - T * 1.8)).r;
b += (n - 0.5) * 0.06 * saturate(t * 3.0);
// les voies se resserrent vers la queue : les langues balaient vers l'axe
float G = 1.0 + 0.18 * smoothstep(0.0, 0.18, t) - 0.42 * saturate(t);
float aa = max(fwidth(b), fwidth(y)) * 1.1 + 0.004;

float rs[4] = { 1.0, 0.86, 0.68, 0.50 };
// langues : position latérale (voie), demi-largeur, longueur ; couches 0..3 = 5 + 4 + 3 + 1
float3 lanes[13] = {
    float3(-0.74, 0.38, 0.55), float3(-0.36, 0.42, 0.80), float3(0.00, 0.44, 0.95), float3(0.38, 0.42, 0.76), float3(0.76, 0.38, 0.50),
    float3(-0.58, 0.40, 0.46), float3(-0.20, 0.42, 0.70), float3(0.22, 0.42, 0.64), float3(0.60, 0.38, 0.42),
    float3(-0.30, 0.40, 0.40), float3(0.04, 0.44, 0.55), float3(0.34, 0.38, 0.34),
    float3(0.00, 0.36, 0.20) };
float dk[4];
for (int k = 0; k < 4; k++) {
    float Rk = R * rs[k];
    dk[k] = length(float2(b, y - (c - (R - Rk) * 0.6))) - Rk;
}
for (int j = 0; j < 13; j++) {
    int k = j < 5 ? 0 : (j < 9 ? 1 : (j < 12 ? 2 : 3));
    float3 L = lanes[j];
    float Lj = L.z * (0.86 + 0.14 * sin(T * (6.0 + j * 1.3) + j * 2.1 + Rand * 6.2832));
    float wob = 0.07 * sin(t * 9.0 - T * 12.0 + j * 1.7) * saturate(t * 2.0);
    float hw = L.y * smoothstep(0.0, 0.14, t) * pow(saturate(1.0 - t / Lj), 0.6);
    float Rt = R * rs[k] * G;
    float dj = (abs(b / Rt - L.x - wob) - hw) * Rt;
    // hors de la langue (devant la tête ou au-delà de sa pointe) : sinon sa ligne centrale resterait à demi visible
    dk[k] = min(dk[k], (t > 0.0 && t < Lj) ? dj : 1.0);
}
// couleurs finales (émissif) : dégradé chaud, jamais blanc ; chaque couche s'assombrit un peu vers sa pointe
float shade = lerp(1.08, 0.80, saturate(t));
float3 cRed = float3(0.95, 0.12, 0.020);
float3 cOrange = float3(1.45, 0.42, 0.035);
float3 cYellow = float3(1.60, 0.85, 0.120);
float3 cCream = float3(1.60, 1.15, 0.420);
float m0 = 1.0 - smoothstep(-aa, aa, dk[0]);
float m1 = 1.0 - smoothstep(-aa, aa, dk[1]);
float m2 = 1.0 - smoothstep(-aa, aa, dk[2]);
float m3 = 1.0 - smoothstep(-aa, aa, dk[3]);
float3 col = cRed * shade;
col = lerp(col, cOrange * shade, m1);
col = lerp(col, cYellow * shade, m2);
// cœur : crème au centre de la tête, jaune sur son bord
float rc = saturate(length(float2(b, y - (c - R * 0.5 * 0.6))) / (R * 0.5));
col = lerp(col, lerp(cCream, cYellow, rc * rc), m3);
// stries de chaleur discrètes qui filent dans les flammes
col *= 1.0 + (n - 0.5) * 0.18 * saturate(t * 4.0);
return float4(col * Tint * Intensity, m0 * Alpha);
