<?php
    /**
     * Created by JetBrains PhpStorm.
     * User: creynder
     * Date: 20/04/12
     * Time: 14:49
     * To change this template use File | Settings | File Templates.
     */


    /**
     * Creates a node of type "checkin", linked to a user account and a RFID reader node
     *
     * @param $params
     *
     * @return stdClass|string
     */
    function scoga_register_checkin( $card_id, $reader_id ){
        module_load_include( 'api.player.inc', 'scoga_core' );
        module_load_include( 'api.reader.inc', 'scoga_rfid' );

        $lang = LANGUAGE_NONE;

        /*
        * get user account
        */
        $user_account = scoga_get_player_entity_by_card_id( $card_id );
        if( $user_account === FALSE ){
            scoga_throw_fatal_error( 'card @card_id not registered', array(
                '@card_id' => $card_id,
            ) );
        }

        /*
        * check if user has correct permissions
        */

        if( user_access( SCOGA_PERM_REST_EXECUTE_CREATE_CHECKIN, $user_account ) === FALSE ){
            scoga_throw_fatal_error( 'user @uid not allowed to create checkin', array(
                '@uid' => $user_account->uid,
            ) );
        }

        /*
        * get reader node by reader ID, this is NOT a nid,
        * this is the unique ID of the reader as hardcoded
        */
        $reader_node = scoga_get_reader_entity_by_reader_id( $reader_id );

        if( !$reader_node ){
            scoga_throw_fatal_error( 'RFID reader with id @reader_id not found', array(
                '@reader_id' => $reader_id,
            ) );
        }


        $cooldown_tstamp = scoga_get_cooldown_timestamp_by_player_uid( $user_account->uid );

        if( time() < $cooldown_tstamp ){
            scoga_throw_fatal_error( 'checkin not created for user with id @uid, will be cooled down on @cooldown_date', array(
                '@uid'           => $user_account->uid,
                '@cooldown_date' => date( 'd/m/Y H:i:s', $cooldown_tstamp ),
            ) );
        }


        /*
        * create checkin node
        */
        $checkin_node = scoga_create_checkin_entity( $lang, $user_account, $reader_node );

        scoga_check_combo_checkin( $user_account, $checkin_node );
        return $checkin_node;
    }


    /**
     * @param $uid
     *
     * @return int
     */
    function scoga_get_cooldown_timestamp_by_player_uid( $uid ){
        $entity_type = 'node';
        $query = new EntityFieldQuery();


        //get last swipe
        $result = $query
            ->entityCondition( 'entity_type', $entity_type )
            ->entityCondition( 'bundle', SCOGA_CHECKIN_NODETYPE )
            ->propertyCondition( 'uid', $uid )
            ->propertyOrderBy( 'created', 'DESC' )
            ->range( 0, 1 )
            ->execute();

        $nids = array_keys( $result[ $entity_type ] );

        if( $nids ){
            $cooldown_period = variable_get( SCOGA_SETTINGS_CHECKIN_COOLDOWN );
            $node = node_load( $nids[ 0 ] );
            return intval( $node->created ) + intval( $cooldown_period );
        }

        return 0;
    }

    function scoga_get_checkin_node_by_index( $from, $order = 'DESC' ){
        $entity_type = 'node';
        $query = new EntityFieldQuery();
        //get last checkin
        $result = $query
            ->entityCondition( 'entity_type', $entity_type )
            ->entityCondition( 'bundle', SCOGA_CHECKIN_NODETYPE )
            ->propertyOrderBy( 'created', $order )
            ->range( $from, 1 )
            ->execute();

        $nids = array_keys( $result[ $entity_type ] );
        if( $nids ){
            return node_load( $nids[ 0 ] );
        }
        return FALSE;
    }

    function scoga_create_checkin_entity( $lang, $user_account, $reader_node ){

        $user_registered = (
            isset( $user_account->field_user_registered )
                && $user_account->field_user_registered[ $lang ][ 0 ][ 'value' ] == "1"
        );

        $checkin_node = new stdClass();
        $checkin_node->type = SCOGA_CHECKIN_NODETYPE;
        node_object_prepare( $checkin_node );

        $checkin_time_formatted = date( SCOGA_TIME_FORMAT, $checkin_node->created );

        //required fields
        $checkin_node->uid = $user_account->uid;
        $checkin_node->language = $lang;

        //optional fields
        //reader reference
        $checkin_node->field_reader_reference[ $lang ][ 0 ] = array(
            'nid' => $reader_node->nid
        );

        //title
        $checkin_node->title = $user_account->name
            . ' checked in with ' . $reader_node->title
            . ' at ' . $checkin_time_formatted;

        //published or not
        $checkin_node->status = ( $user_registered ) ? "1" : "0";

        //save the mofo node
        $checkin_node = node_submit( $checkin_node );
        node_save( $checkin_node );


        return $checkin_node;
    }

    /**
     * @param $user_account creator the checkin node
     * @param $checkin_node newly created 'checkin' node
     *
     * @return bool
     */
    function scoga_check_combo_checkin( $user_account, $checkin_node ){
        $prev_checkin = scoga_get_checkin_node_by_index( 1, 'DESC' );
        if( $prev_checkin->uid === $user_account->uid ){
            //fail if it's the same user
            watchdog( 'SCOGA RFID', 'combo checkin denied: same user' );
            return FALSE;
        }

        $previous = intval( $prev_checkin->created );
        $timeframe = intval( variable_get( SCOGA_SETTINGS_COMBOCHECKIN_TIMEFRAME ) );
        $current = intval( $checkin_node->created );
        /*
            watchdog( 'scoga_check_combo_checkin', 'previous: @previous, timeframe: @timeframe, current: @current', array(
                '@previous' => $previous,
                '@timeframe' => variable_get( SCOGA_SETTINGS_COMBOCHECKIN_TIMEFRAME ),
                '@current' => $checkin_node->created,
            ));
        */
        if( $previous + $timeframe < $current ){
            //fail if too much time has passed between checkins
            watchdog( 'SCOGA RFID', 'combo checkin denied: too much time between checkins' );
            return FALSE;
        }

        $prev_user = user_load( $prev_checkin->uid );
        $prev_team_nid = $prev_user->field_user_team[ LANGUAGE_NONE ][ 0 ][ 'target_id' ];

        if( $prev_team_nid !== $user_account->field_user_team[ LANGUAGE_NONE ][ 0 ][ 'target_id' ] ){
            //fail if the previous checkin was made by a member of the other team
            watchdog( 'SCOGA RFID', 'combo checkin denied: different teams' );
            return FALSE;
        }
        /*
            watchdog( 'SCOGA create relationship between @uid1 and @uid2', array(
                '@uid1' => $user_account->uid,
                '@uid2' => $prev_user->uid,
            ));
        */
        user_relationships_request_relationship( $user_account->uid, $prev_user->uid, SCOGA_CHECKINPARTNER_RTID, TRUE );
        return TRUE;
    }
