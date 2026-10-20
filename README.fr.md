# Caméra GP5_A10S14A5 — Anyka AK3918AV130 — Portage OpenIPC expérimental

Dossier de documentation et de sources préparé le **20 octobre 2026** pour faciliter une discussion avec les mainteneurs OpenIPC. La documentation technique est en anglais pour être directement exploitable en amont.

## Contenu

- Identification du matériel, architecture ARMv5, mémoire réservée, partitions NOR et ABI des pilotes.
- Code C de la passerelle, vidéo/audio natifs, aperçu navigateur, contrôles GPIO/ISP, moteurs, positions et démarrage.
- Interface OpenIPC adaptée, intégration Home Assistant 0.3.0 et carte de commandes.
- Scripts de construction, configurations, sources historiques, dépendances et notices tierces.
- Résultats mesurés, limites, dépannage, récupération et plan de validation.
- Projet de demande aux mainteneurs et répartition des contributions entre builder/wiki/firmware/linux/ipctool.

[Sommaire complet](docs/README.md) · [Guide pratique français](docs/guide-fr.md) · [Sources](docs/source-map.md) · [Résultats](docs/validation.md) · [Plan OpenIPC](docs/upstream.md)

## Statut réel

Le firmware C fonctionne depuis la mémoire interne sur la caméra de développement, avec le noyau et plusieurs pilotes propriétaires d'origine. Les essais courts sont décrits précisément. La stabilité prolongée de la dernière version et un démarrage physique sans SD après coupure restent à démontrer.

Ce dépôt n'est **ni une version officielle OpenIPC, ni une image universelle à flasher**. Les bibliothèques/modules/modèles propriétaires, sauvegardes NOR, identifiants et réglages privés sont exclus. Leur inventaire permet d'identifier les dépendances manquantes sans les redistribuer.

L'intégration officielle exige notamment de remplacer le mécanisme LD_PRELOAD actuellement employé, de résoudre les licences/provenances et de rendre la construction complète reproductible. Le choix de licence du code original reste à confirmer ; les licences existantes des composants tiers sont conservées.

Aucune modification de la caméra, aucun flash et aucune publication distante ne sont effectués par cette préparation documentaire.

## Images et sauvegarde NOR

Les images disponibles dans le README anglais sont expurgées : scène vidéo, noms de positions et identifiants réseau sont masqués. Le dump binaire complet de la mémoire NOR n’est pas publié, car il peut contenir des secrets propres à la caméra et des éléments propriétaires. Son organisation est décrite dans le [plan des partitions NOR](docs/flash-layout.md) ; l’unité observée possède 8 MiB et MTD6 recouvre toute la puce.
