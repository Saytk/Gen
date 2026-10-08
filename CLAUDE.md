# Gen — consignes pour Claude Code

## Travail dans l'éditeur Unreal : passer par VibeUE

Le plugin VibeUE (Plugins/VibeUE) est installé et étend le serveur MCP d'Unreal (`unreal-mcp`).
Pour toute tâche qui touche l'éditeur (Blueprints, Gameplay Tags, Enhanced Input, Niagara, UMG,
matériaux, PIE, profiling...) :

1. Charger d'abord le skill VibeUE du domaine : `call_tool` sur `ToolsetRegistry.AgentSkillToolset`
   → `ListSkills` / `GetSkills` (ex. `VibeUE_gas`, `VibeUE_blueprint_graphs`, `VibeUE_pie_testing`).
2. Agir via `execute_python_code` avec les services VibeUE (`unreal.BlueprintService`,
   `unreal.GameplayTagService`, `unreal.InputService`, `unreal.PIEActorService`...), en complément
   des toolsets natifs d'Epic (EditorAppToolset pour le PIE, AbilitySystemInspectorToolset pour le GAS).
3. Vérifier en PIE (2 clients + serveur dédié) avant d'annoncer qu'une fonctionnalité marche.
4. Dans le récapitulatif final, dire quels outils VibeUE ont été utilisés.

Le code C++ (Source/Gen) s'édite normalement avec les outils fichiers ; VibeUE ne sert pas à ça.

## Verrouillage Git LFS des assets

Les `.uasset` et `.umap` sont `lockable` (voir `.gitattributes`) : deux personnes travaillent sur le
projet et ces fichiers ne se fusionnent pas. Avant de modifier un asset (via VibeUE ou autrement) :

1. `git pull` pour partir de la dernière version.
2. `git lfs lock <chemin>` sur chaque `.uasset` / `.umap` qui va être modifié ou créé à la place
   d'un fichier existant. Si un fichier est déjà verrouillé par quelqu'un d'autre (`git lfs locks`),
   s'arrêter et prévenir l'utilisateur — ne jamais utiliser `git lfs unlock --force` sans son accord.
3. Faire la modification.
4. Après le commit et le push, `git lfs unlock <chemin>` sur ces fichiers. Si l'utilisateur ne veut
   pas encore committer, garder les verrous et le lui rappeler dans le récapitulatif.

Quand l'utilisateur annonce qu'il va modifier des assets à la main, proposer de les verrouiller.

## Direction artistique : `Docs/ArtBible.md`

- Avant tout travail visuel (matériaux, VFX, éclairage, personnages, UI), lire les sections concernées de
  `Docs/ArtBible.md`. Le budget de performance (§3.0) prime sur toutes les autres règles.
- Quand l'utilisateur donne un avis sur un rendu (« trop saturé », « j'aime ce feu »...), appliquer la
  procédure du §13 : consigner l'avis dans le taste log, modifier les règles concernées (marquées [TASTE]),
  signaler tout conflit avec le budget ou les tests de lisibilité au lieu de l'appliquer en silence,
  et ajouter une ligne au change log.

## Interface : `Docs/UI_Guidelines.md`

- Avant tout travail d'UI (HUD, barres au-dessus des personnages, télégraphes, menus, widgets UMG/CommonUI,
  remplacement de `AGenHUD`), lire `Docs/UI_Guidelines.md` : tokens (couleurs, typo, espacements),
  disposition du HUD, specs des composants, animations, accessibilité, règles d'implémentation Unreal.
- Aucun widget ne code en dur une couleur, une police ou une taille : tout passe par les tokens.
- Toute PR d'UI passe la checklist du §9. En cas de conflit, `Docs/ArtBible.md` l'emporte.

## Processus d'implémentation : vite d'abord

Objectif : un résultat jouable vite, puis itérer. La prudence se concentre sur ce qui casse le multijoueur ;
tout le reste avance sans cérémonie.

- **Plans courts.** Une spec en puces (valeurs, règles, cas limites) et une liste de tâches de quelques
  lignes chacune. Pas de code recopié dans les plans : les agents écrivent le code. Une décision de design
  = une ligne dans la spec, pas une révision du plan.
- **Décider sans attendre.** Valeur par défaut raisonnable, notée dans la spec ou dans le journal de la
  bible artistique (§13) ; l'utilisateur corrige après coup. Ne demander que pour une action destructive
  ou visible de l'extérieur.
- **Deux espaces de travail au maximum.** L'arbre principal (éditeur, assets) et un seul worktree pour le
  C++. Jamais deux agents sur les mêmes fichiers. Moins de branches = moins de fusions et de conflits.
- **Lots plutôt que tâches.** Un agent prend une fonctionnalité entière (code + tests), une compilation
  et un passage de tests à la fin du lot. Live Coding pour les `.cpp` pendant l'itération.
- **Revue unique et ciblée.** Une seule revue par fonctionnalité, en fin de lot, et seulement pour le
  réseau, l'autorité serveur et la prédiction. Le relecteur ne rapporte que le Critique et l'Important ;
  les mineurs vont dans une liste de nettoyage traitée plus tard, en un seul lot.
- **Tester à la bonne échelle, pas à chaque petit changement.**
  - Un nouveau test seulement pour une règle de gameplay ou réseau (coûts, autorité, prédiction, réplication).
    Pas de test pour un correctif cosmétique, un renommage, un commentaire, une valeur d'asset ou une UI.
  - Pendant l'itération : compiler et lancer seulement les tests du domaine touché
    (`RunTestsByFilter` avec `StartsWith:Gen.Net.CastRules` par exemple), dans l'éditeur ouvert.
  - La suite complète une seule fois, en fin de lot ; le test headless seulement avant de pousser.
- **Tests réseau automatiques comme barrière.** Les tests CQTest `Gen.Net.*` remplacent les matrices PIE
  à chaque tâche. Un seul passage PIE court (2 clients) par fonctionnalité pour l'œil ; la matrice complète
  (3 clients, latence) seulement avant la fusion dans `main`.
- **Ne pas redémarrer l'éditeur pour rien.**
  - Un seul redémarrage par lot de C++ : on fusionne le C++ du worktree dans l'arbre principal à un point de
    synchronisation prévu, puis on ferme, compile et relance une seule fois.
  - Pendant l'itération, Live Coding pour les `.cpp`. Préférer un changement en `.cpp` à un nouveau membre de
    `.h` quand c'est possible, et regrouper les changements de `.h` du lot.
  - Les assets ne se modifient que dans l'éditeur principal, jamais en headless sur une autre branche : pas
    de fusion d'assets binaires, donc pas de fermeture de l'éditeur pour fusionner.
  - Les tests tournent dans l'éditeur ouvert, pas dans un second process Unreal (sauf le test headless avant
    de pousser).
  - Pas de redémarrage « par sécurité » : seulement si une compilation l'exige ou si l'éditeur a planté.
- **Vérifications visuelles groupées à la fin.** Pas de PIE ni de capture après chaque petit changement : une
  seule vérification visuelle quand tout le lot est en place. Exception : la toute première version d'un
  nouveau style visuel (nouvelle famille de VFX, nouveau look) se montre tôt, avec une capture avant/après,
  pour ne pas construire tout un lot dans une direction refusée. Jamais de VFX construits « à l'aveugle » en
  headless puis branchés. Le style de référence
  (bible §7.4, [TASTE #5]) sert de base : on duplique et on adapte.
- **Rapports courts.** Un agent rend 15 lignes maximum : commits, résultat des tests, ce qui reste. Le
  détail va dans un fichier si nécessaire.
- **Boîte de temps.** Une tâche d'agent vise 30 à 45 minutes. Plus long : la découper.

## Itération rapide et sécurité de l'éditeur (agents)

**Compiler le C++**
- **Live Coding, pour les `.cpp` seulement.** Si le changement ne touche que des corps de fonctions dans des
  `.cpp`, compiler à chaud sans fermer l'éditeur : `call_tool` → `LiveCodingToolset.CompileLiveCoding`
  (repli : commande console `LiveCoding.CompileSync`).
  - Après le patch, lancer `ModelContextProtocol.RefreshTools`.
  - Si le log annonce un ré-instanciement (« data type changes may cause packaging to fail »), ne
    sauvegarder aucun asset qui référence ces types avant une vraie compilation.
  - Le patch n'existe qu'en mémoire : avant un commit, il faut une compilation complète et les tests headless.
- **Tout le reste passe par le cycle complet** (fermer l'éditeur, `Build.bat`, relancer) : un `.h` modifié
  (UPROPERTY, UFUNCTION, membre, signature), un nouveau fichier ou une nouvelle classe, un `Build.cs`, une
  valeur par défaut de constructeur ou de CDO.
- **Compiler depuis un worktree** :
  `Build.bat GenEditor Win64 Development -Project="<worktree>\Gen.uproject" -NoHotReloadFromIDE -WaitMutex`.
  - Pour vérifier un seul fichier sans lier : ajouter `-SingleFile="<chemin du .cpp>"`, ou `-Module=Gen`.
  - `-NoHotReloadFromIDE` est réservé aux worktrees : jamais dans l'arbre principal pendant que l'éditeur tourne.
- **UBT** : garder les réglages par défaut. Ne pas mettre `bUseUnityBuild=false` (bug 5.8.1), ni passer
  `-NoUBTMakefiles`.

**Tests**
- **Dans l'éditeur ouvert** : `AutomationTestToolset` → `DiscoverTests` (une fois par session), puis
  `RunTestsByFilter` avec `filterExpression: "StartsWith:Gen."` (il renvoie déjà les résultats).
- **Multijoueur** : les vérifications déterministes (réplication, GAS, autorité serveur) s'écrivent en CQTest
  `NETWORK_TEST_CLASS` dans le module éditeur `GenTests`, qui fait tourner un serveur dédié et des clients
  dans un seul process (guide : `Docs/Dev/CQTestNetworkTests.md`). Le PIE manuel (2 clients + serveur dédié)
  reste la vérification finale, à l'œil.
  - Lancés dans l'éditeur ouvert, ces tests remplacent le niveau courant par une carte vide, sans demander.
    Il faut donc sauvegarder avant, puis rouvrir `L_Arena` après.
- **Test headless, seulement comme barrière avant de pousser** (inutile si l'éditeur a été relancé sur la
  compilation finale sans Live Coding depuis, et que la suite complète y est passée) :
  `UnrealEditor-Cmd.exe "<projet>\Gen.uproject" -ExecCmds="Automation RunTests Gen.;Quit" -unattended -nullrhi -nosplash -nosound -stdout -ReportExportPath="<projet>\Saved\TestReport" -ModelContextProtocolPort=8011`.
  Lire ensuite `index.json` dans le dossier du rapport, pas le log. `Tools/RunGenTests.ps1` fait les deux.

**Moins d'allers-retours MCP** (détails et sources : `.superpowers/sdd/research-mcp-speed.md`)
- `.mcp.json` porte `"timeout": 900000` (15 min) : sans lui, Claude Code coupe un appel long (tests, Live Coding)
  au bout de 60 s sans premier octet ou 5 min sans réponse, puis peut le relancer pendant que le premier tourne
  encore. `ModelContextProtocol.GenerateClientConfig` réécrit le fichier : remettre ce champ après.
- `RunTestsByFilter` renvoie déjà le JSON complet par test : ne pas appeler `GetTestResults` ensuite.
- Enchaîner plusieurs outils asynchrones en un seul appel avec `ProgrammaticToolset.execute_tool_script`
  (ex. `CompileLiveCoding` → `RunTestsByFilter` → verdict compact) : l'éditeur continue de tourner entre les
  étapes, ce que `execute_python_code` ne permet pas.
- Un script Python fait plusieurs étapes et renvoie un JSON compact ; les fonctions réutilisables vont dans
  un module du projet (`Content/Python/gen_*.py`), importé une fois puis `importlib.reload` après édition,
  au lieu de renvoyer le même code à chaque appel.
- Garder les sorties petites : `capture_image` avec `max_width: 800`, pas de `describe_toolset` complet.
- PIE piloté par script : couper le ralentissement en arrière-plan
  (`unreal.PerformanceService.set_background_throttling(False)`), sinon l'éditeur sans focus tourne à ~3 FPS.
  Le remettre à la fin.
- Passage PIE court : VibeUE `WorkflowService.run_scenario` (un appel, PIE toujours arrêté à la fin,
  assertions avec valeurs attendues et obtenues).
- Après un relancement, attendre l'éditeur via les fichiers `Saved/VibeUE/Signals/editor-<pid>-*.json`
  (prêt, carte courante, game thread bloqué) plutôt qu'en relançant des appels MCP.
- Un appel Python qui a expiré : lire `vibeue.last_python_result()` au lieu de le relancer.
- Nouveau réglage en cours d'itération : d'abord une CVar déclarée dans un `.cpp` (Live Coding, sans
  redémarrage), promue en UPROPERTY dans l'unique changement de `.h` du lot.
- Assets : régler les paramètres d'une instance de matériau plutôt que le matériau maître (pas de
  recompilation de shaders), lire l'Asset Registry sans charger les assets, compiler chaque Blueprint une
  seule fois par lot.

**Sécurité de l'éditeur**
- **Ne jamais piloter l'UI de l'éditeur** (clics, saisie, glisser). `SlateInspectorToolset` est désactivé
  dans `DefaultEngine.ini`, parce qu'un modal ouvert pendant un appel MCP gèle l'éditeur.
- **Un seul appel MCP à la fois par éditeur** : jamais d'appels en parallèle, ni deux sous-agents sur le
  même éditeur.
- **Un port MCP par process d'éditeur.** L'éditeur principal écoute sur 8010 (réglage par utilisateur,
  dans `Saved`). Tout autre éditeur du projet (headless, worktree) se lance avec
  `-ModelContextProtocolPort=8011` (puis 8012...).
- **VibeUE exécute le Python en mode « unattended »** : les boîtes de dialogue prennent leur réponse par
  défaut au lieu de bloquer. C'est un patch local du sous-module, `Tools/Patches/VibeUE-unattended-python.patch`.
  Après chaque mise à jour de VibeUE, le réappliquer
  (`git -C Plugins/VibeUE apply ../../Tools/Patches/VibeUE-unattended-python.patch`), puis recompiler.
- **`Config/DefaultEditorPerProjectUserSettings.ini`** : pas d'invite de restauration des onglets, et pas
  d'ajout automatique au contrôle de source. Committer par chemins explicites.
- **Éditions d'assets en lot** : prendre le verrou LFS d'abord. Ensuite, soit de petits appels
  `execute_python_code` en série, soit le commandlet
  `UnrealEditor-Cmd.exe Gen.uproject -run=pythonscript -script="<fichier.py>" -ModelContextProtocolPort=8011`,
  mais seulement pour des assets que l'éditeur ouvert n'a pas chargés.

## Exceptions propres à ce projet (prioritaires sur le guide VibeUE ci-dessous)

- Ne PAS lancer `Plugins/VibeUE/BuildAndLaunchGame.ps1` : il tue l'éditeur de force (taskkill /F)
  et vide `Saved/Logs`. Pour compiler le C++ (hors Live Coding) :
  1. vérifier qu'aucun asset n'est modifié sans être sauvegardé ;
  2. fermer l'éditeur proprement (`unreal.SystemLibrary.quit_editor()`) ;
  3. compiler avec `Build.bat` ;
  4. relancer l'éditeur. Claude peut le faire lui-même (accord de l'utilisateur du 2026-10-07), avec le
     chemin complet de `UnrealEditor.exe` suivi de `Gen.uproject` : l'`EngineAssociation` du projet ne
     correspond pas à toutes les installations.
- `execute_python_code` : passer `auto_save: false` sauf si l'utilisateur veut tout sauvegarder.
- Répondre en français.

<!-- BEGIN VibeUE (v5.0) — generated by VibeUE.GenerateAgentConfig; re-run to refresh -->
@Plugins/VibeUE/Content/samples/AGENTS.md.sample
<!-- END VibeUE -->
