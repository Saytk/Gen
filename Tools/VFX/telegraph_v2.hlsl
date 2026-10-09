// Télégraphe v2 (2026-10-09, « indicateurs plus propres et pros ») : bords anti-crénelés au pixel, halo lumineux
// sous le liseré, remplissage plus dense au bord qu'au centre, minuteur lisible (zone écoulée plus dense + front
// lumineux), rayures ennemies douces, bouts d'arc adoucis. Mêmes entrées et même géométrie que la v1.
// Coordonnées locales du plan : UV -> [-SizeX, SizeX] x [-SizeY, SizeY] (cm). +X local = direction de visée.
float2 L = (UV * 2.0 - 1.0) * float2(SizeX, SizeY);
float R = length(L);
float AX = max(SizeX, 1e-4);
bool IsDisc = Shape < 0.5;
bool IsLane = Shape >= 0.5 && Shape < 1.5;
bool IsArc = Shape >= 1.5 && Shape < 2.5;
bool IsStub = Shape >= 2.5 && Shape < 3.5;
bool IsSpokes = Shape >= 3.5;

// Distance signée au bord (négative dedans), continue partout : fwidth la lit avant tout masque
float2 Q = abs(L) - float2(SizeX, SizeY);
float BoxD = length(max(Q, 0.0)) + min(max(Q.x, Q.y), 0.0);
float CapX = max(SizeX - SizeY, -SizeX);                     // couloir : départ plat, bout arrondi de rayon SizeY
float LaneD = L.x > CapX ? length(L - float2(CapX, 0.0)) - SizeY : BoxD;
float D = IsLane ? LaneD : (IsStub ? BoxD : R - SizeX);
float Px = max(fwidth(D), 1e-5);
float PxR = max(fwidth(R), 1e-5);
float BorderW = BorderWidthPx * Px;
float KeyW = KeylineWidthPx * Px;

// [TASTE #13] Soi et alliés partagent le bleu allié au sol (plus de blanc) ; l'autre équipe en rouge (MPC_TeamColours)
float3 Rel = RelationIndex < 2.5 ? AllyColour.rgb : (RelationIndex < 3.5 ? EnemyColour.rgb : NeutralColour.rgb);
float3 FirstKey = KeylinePolarity < 0.5 ? KeylineColour.rgb : KeylineLightColour.rgb;
float SecondKeyW = KeylinePolarity > 1.5 ? KeyW : 0.0;

float A = atan2(L.y, L.x);
float Fade = IsStub ? 1.0 - smoothstep(0.2, 1.0, L.x / AX) : 1.0;     // amorce : s'efface sur les derniers 40 %
Fade *= IsArc ? saturate((0.5236 - abs(A)) / 0.1) : 1.0;              // arc de portée : ±30°, bouts fondus sur ~6°

if (IsSpokes)
{
    // 6 traits radiaux entre 0.7 R et R (expulsion), 3 px, anti-crénelés
    float Sector = 1.0471976;
    float Off = abs(A - Sector * round(A / Sector)) * R;
    float Line = saturate(2.0 - Off / PxR) * saturate((R - 0.7 * SizeX) / PxR + 0.5) * saturate((SizeX - R) / PxR + 0.5);
    return float4(Rel, BorderAlpha * Line);
}

// Bandes depuis le bord, en distance intérieure, chacune avec une transition d'un pixel
float In = -D;
float E0 = saturate(In / Px + 0.5);
float E1 = saturate((In - BorderW) / Px + 0.5);
float E2 = saturate((In - BorderW - KeyW) / Px + 0.5);
float E3 = saturate((In - BorderW - KeyW - SecondKeyW) / Px + 0.5);
float ABorder = (E0 - E1) * BorderAlpha;
float AKey1 = (E1 - E2) * BorderAlpha;
float AKey2 = (E2 - E3) * BorderAlpha;

// Remplissage : plus dense près du bord qu'au centre (lecture de la forme), halo doux sous le liseré
float Depth = max((IsLane || IsStub) ? SizeY : AX, 1e-4);
float Grad = lerp(1.25, 0.4, saturate(In / (0.6 * Depth)));
float Glow = exp(-max(In - BorderW - KeyW - SecondKeyW, 0.0) / (GlowFalloffPx * Px));   // [TASTE #13] liseré qui s'efface vers l'intérieur
float FillBase = IsArc ? 0.0 : FillAlpha * Grad;
float GlowA = Glow * (IsArc ? BorderAlpha * 0.35 : FillAlpha * 1.0);

// Minuteur (disque) : zone écoulée plus dense, front lumineux à l'avancée
float Timer = 1.0;
float FrontA = 0.0;
if (IsDisc && Fill < 0.999)
{
    float Rf = Fill * AX;
    Timer = lerp(0.55, 1.45, saturate((Rf - R) / PxR + 0.5));
    FrontA = saturate(1.0 - abs(R - Rf) / (1.25 * PxR)) * BorderAlpha * 0.85;
}

// Rayures ennemies (diagonales), anti-crénelées
float Stripes = 1.0;
if (EnemyPattern > 0.5)
{
    float S = (L.x + L.y) * 6.0 / AX;
    float SW = max(fwidth(S), 1e-4);
    Stripes = lerp(0.6, 1.0, saturate((abs(frac(S) - 0.5) * 2.0 - 0.5) / (2.0 * SW) + 0.5));
}

float AFill = E3 * saturate(FillBase * Timer * Stripes + GlowA + FrontA);
float Sum = ABorder + AKey1 + AKey2 + AFill;
float3 Col = (Rel * (ABorder + AFill) + FirstKey * AKey1 + KeylineColour.rgb * AKey2) / max(Sum, 1e-4);
return float4(Col, saturate(Sum) * Fade);
