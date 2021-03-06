<?php

class SearchlightEnvironment {
  // Storage variables: saved to DB or code when an environment is saved.
  var $name;
  var $view_display;
  var $facets;
  var $options;

  // Working variables: populated at various init and other stages.
  var $datasource;
  var $enabled;
  var $active_values;

  // Class objects.
  var $plugins;
  var $query;
  var $view;

  function construct() {
    $this->name = isset($this->name) ? $this->name : '';
    $this->view_display = isset($this->view_display) ? $this->view_display : '';
    $this->facets = !empty($this->facets) ? $this->facets : array();
    $this->options = !empty($this->options) ? $this->options : array();
    $this->options = $this->options + array(
      'facets_label' => t('Search facets'),
    );

    $this->enabled = array();
    $this->active_values = array();

    $this->plugins = array();
    $this->query = NULL;
    $this->view = NULL;
  }

  /**
   * Activate this environment using the values found (and parsed) from the
   * Searchlight facet querystring. See searchlight_environment_init().
   */
  function initValues($values) {
    $this->active_values = $values;

    // Remove the environment key which is for internal use only.
    if (isset($this->active_values['environment'])) {
      unset($this->active_values['environment']);
    }
  }

  /**
   * Initialize the environment view and all plugins.
   */
  function initView($view = NULL, $reset = FALSE) {
    if (!isset($this->view) || $reset) {
      $this->view = NULL;
      $this->datasource = NULL;

      // Build the view if none was explicitly provided.
      $view = isset($view) ? $view : searchlight_build_view($this->view_display);

      // Create a reference to the view and init plugins.
      if ($view) {
        $this->view = $view;
        $this->query = $view->query;
        $this->datasource = isset($view->query->datasource) ? $view->query->datasource : NULL;
        $this->plugins = array();

        foreach ($this->getFacets() as $name => $facet) {
          $field = isset($this->datasource->fields[$name]) ? $this->datasource->fields[$name] : array();
          $plugin = searchlight_get_facet($this->datasource, $name);
          $plugin->construct($this, $field, $this->getValue($name), $this->facets[$name]);
          $this->plugins[$name] = $plugin;
        }
      }
    }
    return !empty($this->view);
  }

  /**
   * Allow the environment to initialize plugins and allow them to modify the
   * Views query.
   */
  function query(&$query) {
    // If any facets are active treat it as though the result set is not empty.
    if ($this->getFacets(TRUE)) {
      $query->set_search_buildmode('search', TRUE);
    }
    foreach ($this->plugins as $name => $plugin) {
      $plugin->query($query);
    }
  }

  /**
   * Generate a set of renderable facet items.
   */
  function render($deltas = array('active', 'facets')) {
    $rendered = array();
    foreach ($this->plugins as $name => $plugin) {
      foreach ($deltas as $delta) {
        if ($render = $plugin->render(clone $this->query, $delta)) {
          // Theme each item
          $items = array();
          foreach ($render as $item) {
            $items[] = $plugin->theme($item, $delta);
          }
          // Generate a label/items array suitable for theme('searchlight_facet')
          $rendered[$name] = array(
			'name' => $name,
            'label' => $plugin->label($delta),
            'items' => $items,
            'delta' => $delta,
          );
        }
      }
    }
    return $rendered;
  }

  /**
   * Retrieve the active value for a given facet.
   */
  function getValue($key) {
    $facets = $this->getFacets();
    if (isset($this->active_values[$key]) && isset($facets[$key])) {
      return $this->active_values[$key];
    }
    return NULL;
  }

  /**
   * Build a block suitable for return by hook_block('view').
   */
  function getBlock($delta) {
    if ($delta === 'facets' && $this->initView()) {
      // Provide an array with 'unthemed' data for modules implementing
      // hook_block_view_alter() in order to have more theming flexibility
      $data = array();
      $output = '';
      $data['container']['name'] = $this->name;
      $data['container']['view_name'] = $this->view->name;
      $data['container']['facets'] = array();
      foreach ($this->render() as $rendered) {
        $data['container']['facets'][] = $rendered;
        // If we don't get back arrays for items, we directly them them
        if (!is_array($rendered['items'][0])) {
          $output .= theme('searchlight_facet', array('facet' => $rendered));
        }
      }
      if (!empty($output)) {
        $output = "<div class='searchlight-environment searchlight-environment-{$this->name} searchlight-view-{$this->view->name}-{$this->view->current_display}'>{$output}</div>";
        $data['label'] = filter_xss_admin($this->options['facets_label']);
        $data['content'] = $output;
      }
      return $data;
    }
    return array();
  }

  /**
   * Get the datasource associated with this environment's view.
   */
  function getDatasource($reset = FALSE) {
    // Adapted from searchlight_build_view. It should be possible to
    // retrieve a data source from a view without running it, untill then...
    $split = explode(':', $this->view_display);

    if (count($split) === 2 && $view = views_get_view($split[0])) {
      return searchlight_get_datasource($view->base_table);
    }
    return FALSE;
  }

  /**
   * Retrieve enabled facets for this environment.
   */
  function getFacets($active_only = FALSE) {
    if (empty($this->enabled)) {
      foreach ($this->facets as $name => $facet) {
        if (!empty($facet['enabled']) && $this->isValidFacet($name)) {
          // Set set #weight so element_sort can be used.
          $facet['#weight'] = isset($facet['weight']) ? $facet['weight'] : 0;
          $this->enabled[$name] = $facet;
        }
      }
      uasort($this->enabled, 'element_sort');
    }
    return $active_only ? array_intersect_key($this->enabled, $this->active_values) : $this->enabled;
  }

  /**
   * Options form for configuring this environment.
   */
  function optionsForm(&$form, &$form_state) {
    views_include('admin');
    views_include('form');

    $form['#theme'] = 'searchlight_admin_environment';
    $form['#environment'] = $this;

    $form['options']['#tree'] = TRUE;
    $form['options']['facets_label'] = array(
      '#type' => 'textfield',
      '#title' => t('Facets label'),
      '#description' => t('Subject for the facets block in this environment.'),
      '#default_value' => $this->options['facets_label'],
    );

    // Include searchlight.admin.inc for views displays function.
    module_load_include('inc', 'searchlight', 'searchlight.admin');
    $form['view_display']['#tree'] = TRUE;
    $form['view_display']['view_display'] = array(
      '#title' => t('View'),
      '#type' => 'select',
      '#default_value' => $this->view_display,
      '#options' => searchlight_admin_environment_view_displays(),
    );
    $form['view_display']['update'] = array(
      '#value' => t('Update view'),
      '#type' => 'submit',
      '#submit' => array('searchlight_admin_environment_edit_submit'),
    );

    $datasource = $this->getDatasource();
    if ($datasource) {
      $form['facets'] = array('#tree' => TRUE);
      $fields = $datasource->fields;
      $fields['search_query'] = array(
        'label' => t('Search query'),
        'name' => 'search_query'
      );
      foreach ($fields as $name => $field) {
        if ($this->isValidFacet($name)) {
          $form['facets'][$name] = array(
            '#tree' => TRUE,
            '#weight' => isset($this->facets[$name]['weight']) ? $this->facets[$name]['weight'] : 0,
          );
          $form['facets'][$name]['enabled'] = array(
            '#title' => t('Enabled'),
            '#type' => 'checkbox',
            '#default_value' => isset($this->facets[$name]['enabled']) ? $this->facets[$name]['enabled'] : FALSE,
          );
          $form['facets'][$name]['ui_name'] = array(
            '#title' => t('Facet'),
            '#markup' => $field['label'],
          );
          $form['facets'][$name]['weight'] = array(
            '#title' => t('Weight'),
            '#type' => 'weight',
            '#default_value' => isset($this->facets[$name]['weight']) ? $this->facets[$name]['weight'] : 0,
          );

          // We instantiate plugins here rather than using initView() as the
          // facets on this form may not already be enabled for this environment.
          $plugin = searchlight_get_facet($datasource, $name);
          $plugin->construct($this, $field, $this->getValue($name), isset($this->facets[$name]) ? $this->facets[$name] : array());

          $plugin->optionsForm($form['facets'][$name], $form_state);

          $form['facets'][$name]['settings'] = array(
            '#tree' => TRUE,
            '#title' => t('Settings'),
          );
          $plugin->extendedForm($form['facets'][$name]['settings'], $form_state);
        }
      }
    }
  }

  /**
   * Submit handler for options form.
   */
  function optionsSubmit(&$form, &$form_state) {
    switch ($form_state['clicked_button']['#id']) {
      case 'edit-view-display-update':
        $this->view_display = $form_state['values']['view_display']['view_display'];
        searchlight_environment_save($this);
        break;
      case 'edit-save':
        $this->facets = $form_state['values']['facets'];
        // Do additional processing.
        foreach ($this->facets as $name => $options) {
          if (isset($options['settings'])) {
            unset($this->facets[$name]['settings']);
            $this->facets[$name] = array_merge($this->facets[$name], $options['settings']);
          }
          if (empty($options['enabled'])) {
            unset($this->facets[$name]);
          }
        }

        $this->options = $form_state['values']['options'];

        searchlight_environment_save($this);
        drupal_set_message(t('Environment @environment saved.', array('@environment' => $this->name)));
        break;
    }
  }

  /**
   * Determine whether a given facet is valid.
   */
  function isValidFacet($name) {
    if ($name === 'search_query') {
      return TRUE;
    }
    else if ($datasource = $this->getDatasource()) {
      return (isset($datasource->fields[$name]) && in_array($datasource->fields[$name]['usage'], array('attribute', 'multivalue'), TRUE));
    }
  }

  /**
   * Get the URL path for this environment.
   */
  function getURLPath() {
    if ($this->initView()) {
      return $this->view->get_url();
    }
    return $_GET['q'];
  }

  /**
   * Get the URL options for the current set of active facets, adjusted using
   * one of the $op operations.
   *
   * 'add':    Add a facet value or multiple facet values for the given key/value pair.
   * 'remove': Add a facet value or multiple facet values for the given key/value pair.
   * 'active': Retain only active facets and drop any other query strings.
   */
  function getURLOptions($op = 'add', $key = NULL, $value = NULL) {
    $options = array();
    $modifier = array('environment' => $this->name);
    switch ($op) {
      case 'add':
        $modifier = $modifier + $this->active_values;
        $modifier[$key] = $value;
        break;
      case 'remove':
        $modifier = $modifier + $this->active_values;
        if (is_array($modifier[$key]) && !empty($value)) {
          foreach ($modifier[$key] as $k => $v) {
            if ($v === $value) {
              unset($modifier[$key][$k]);
            }
          }
        }
        else {
          if (isset($modifier[$key])) {
            unset($modifier[$key]);
          }
        }
        break;
      case 'active':
        $modifier = $modifier + $this->active_values;
        break;
    }

    $key = variable_get('searchlight_facet_key', 'sl');
    $query = $_GET;
    $modifier = searchlight_environment_pack($modifier);
    if (!empty($modifier)) {
      $query[$key] = $modifier;
    }
    elseif (isset($query[$key])) {
      unset($query[$key]);
    }

    $exclude = array('q', 'page');
    // If we've been asked to retain all facets, we need to exclude all
    // query strings **except** the facet query string.
    if ($op === 'active') {
      $exclude = drupal_map_assoc(array_keys($_GET));
      if (isset($exclude[$key])) {
        unset($exclude[$key]);
      }
    }
    // If there is no modifier we want to exclude the facet query string
    // (forces query string to be dropped).
    else if (empty($modifier)) {
      $exclude[] = $key;
    }
    $options['query'] = drupal_get_query_parameters($query, $exclude);

    return $options;
  }
}
