<?php
/**
 * @file
 * scoga_core_feature.features.user_role.inc
 */

/**
 * Implements hook_user_default_roles().
 */
function scoga_core_feature_user_default_roles() {
  $roles = array();

  // Exported role: player
  $roles['player'] = array(
    'name' => 'player',
    'weight' => '2',
  );

  return $roles;
}