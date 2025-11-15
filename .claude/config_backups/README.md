# Claude Code Configuration Backups

Ce répertoire contient les sauvegardes des modifications apportées aux fichiers de configuration de Claude Code.

## Modifications effectuées

### 2025-11-15 - Désactivation du serveur MCP Blender

**Fichier modifié :** `%APPDATA%\Claude\claude_code_config.json`

**Raison :** Réduire l'usage de contexte (économie de ~5.9% / 11.9k tokens)

**Modification :**
- ❌ Supprimé : Serveur MCP `blender` (17 outils)
- ✅ Conservé : Serveur MCP `n8n-local`

**Sauvegarde :** `claude_code_config_backup_2025-11-15.json` (version originale avec Blender)

**Pour restaurer Blender :**
```bash
# Copier la sauvegarde vers le fichier de config
cp .claude/config_backups/claude_code_config_backup_2025-11-15.json \
   "/mnt/c/Users/Alexis Trouvé/AppData/Roaming/Claude/claude_code_config.json"

# Puis redémarrer Claude Code
```

---

**Impact :**
- Avant : 17 outils MCP Blender chargés (11.9k tokens)
- Après : 0 outils Blender (gain de ~6% de contexte)
- Serveur n8n-local toujours actif
