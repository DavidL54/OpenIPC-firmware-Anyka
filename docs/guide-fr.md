# Guide pratique français

## Utiliser ce qui fonctionne déjà

L'interface Aperçu permet les mouvements manuels, Arrêt, huit positions nommées et le choix au démarrage. Une position est enregistrée à partir de la position réelle comptée par le pilote. Le choix « centre » correspond à startup=0. Le rappel au démarrage intervient après le calibrage normal une seule fois par démarrage ; relancer la vidéo ne déplace pas de nouveau la caméra.

Home Assistant utilise les mêmes positions stockées dans la caméra. Installer le dossier anyka_controls dans custom_components, la carte JavaScript dans www, redémarrer HA puis ajouter l'intégration et sa ressource de tableau de bord. Le mot de passe est renseigné côté serveur HA. Le composant actuel est prévu pour une seule caméra ; les détails se trouvent dans le chapitre Home Assistant.

Le flux principal est /vs0, le sous-flux /vs1. Utiliser RTSP TCP avec Digest et conserver des identifiants privés. Deux lecteurs publics au maximum sont autorisés ; fermer un lecteur de diagnostic avant de chercher un problème HA. L'aperçu navigateur a sa propre limite à un lecteur.

## Comprendre les limites

La mémoire interne est une NOR de 8 Mio, différente de la SD en capacité et en comportement de lecture/cache. Le passage en C a permis de réduire l'empreinte du programme utilisateur tout en conservant le SDK vidéo compatible. Le noyau, le capteur et les moteurs restent ceux validés sur cette carte.

Les tests audio silencieux vérifient le protocole et la survie du processus ; ils ne prouvent pas la qualité du haut-parleur. Le fonctionnement d'un NPU sans erreur ne prouve pas la reconnaissance d'une personne. Un processus resté actif quatorze heures ne prouve pas quatorze heures de vidéo décodée sans défaut.

## Pour contribuer à OpenIPC

Commencer par l'identification et les preuves matérielles. Utiliser le projet de demande upstream/ISSUE-DRAFT.md après revue. Les mainteneurs doivent valider une architecture qui remplace LD_PRELOAD et les conditions de distribution du SDK. Le dossier fournit les sources originales et les dépendances libres ; il ne peut fournir un code propriétaire absent ni accorder une licence à son propriétaire.

Ne jamais envoyer une sauvegarde complète de la caméra ou un fichier de configuration contenant Wi-Fi, mot de passe, HA1 ou clés SSH. Les procédures historiques ne sont pas un installateur générique. Consulter les chapitres de sécurité et de récupération avant toute nouvelle opération matérielle.

[Sommaire](README.md) · [Accueil français](../README.fr.md)
