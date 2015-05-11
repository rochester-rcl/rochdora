<?php
/**
 * @file
 * Batch integration for Rochester.
 */

/**
 * Rochester customizations on the scan batch preprocessor.
 *
 * @see IslandoraScanBatch
 */
class RochdoraBatchPreprocessor extends IslandoraScanBatch {
  /**
   * Scan the directory with file_scan_directory().
   *
   * Gets all the base volumes to be constructed.
   */
  protected function scanDirectory($target) {
    $files = array();
    $target_path = drupal_realpath($target) . "/Phase1";
    $target_length = strlen($target_path) + 1;
    $directory_contents = file_scan_directory(
      $target_path,
      $this->getPattern(),
      array('recurse' => FALSE)
    );
    foreach ($directory_contents as $uri => $value) {
      $files[substr($uri, $target_length)] = $value;
    }
    return $files;
  }

  // @TODO: Scan ZIP.


  /**
   * Get the associated object class.
   *
   * @see IslandoraScanBatch::getObjectClass()
   */
  protected static function getObjectClass() {
    return 'RochdoraBatchVolumeObject';
  }
}



class RochdoraBatchVolumeObject extends IslandoraScanBatchObject {
  /**
   * Goes and gets the first child (Volumes) of Rochester's data model.
   *
   * Assume that the un
   */
  public function getChildren(IslandoraTuque $connection) {
    module_load_include('inc', 'rochdora', 'includes/utilities');
    $children = array();
    $target = $this->preprocessorParameters['target'] . '/Phase3/' . $this->baseName;
    $mask = "/(.*)_p3\.tif/";
    $pages = rochdora_retrieve_files_for_object($target, $mask);
    $sequence_number = 1;
    foreach ($pages as $page) {
      $children[] = new RochdoraBatchPageObject($connection, $this->id, $sequence_number, $page, $this->preprocessorParameters, $this->baseName);
      $sequence_number = $sequence_number + 1;
    }

    // Go get all unoccluded pages to serve as the pages of the diary.
    return $children;
  }

  /**
   * Add collection and content model relationships.
   */
  public function addRelationships() {
    $collection = $this->preprocessorParameters['parent'];
    $predicate = $this->preprocessorParameters['parent_relationship_pred'];
    $uri = $this->preprocessorParameters['parent_relationship_uri'];

    $this->relationships->add($uri, $predicate, $collection);
    $this->addContentModelRelationships();
  }

  /**
   * Add the content model relationship(s).
   */
  protected function addContentModelRelationships() {
    $this->models = array('islandora:bookCModel');
  }

  /**
   * Function batch_process.
   */
  public function batchProcess() {
    $this->label = $this->getTitle();
    $this->getMods();
    $this->getDc();
    return ISLANDORA_BATCH_STATE__DONE;
  }

  /**
   * Function to get the mods.
   */
  protected function getMods() {
    if (!isset($this['MODS'])) {
      module_load_include('inc', 'rochdora', 'includes/utilities');
      // TODO: Create the MODS datastream on this object.
      $mods_datastream = $this->constructDatastream('MODS', 'M');
      $mods_datastream->mimetype = 'application/xml';
      $mods_datastream->label = 'MODS Record';

      // Go and attempt to retrieve the MODS for the volume.
      $mods_files = rochdora_retrieve_files_for_object("{$this->preprocessorParameters['target']}/Phase5/metadata/{$this->baseName}", '/(.*)\.mods/');
      if (!empty($mods_files)) {
        $mods_file = reset($mods_files);
        $mods_datastream->setContentFromFile($mods_file->uri);
      }
      else {
        return FALSE;
      }
      $this->ingestDatastream($mods_datastream);
    }

    return $this['MODS']->content;
  }
  /**
   * Function to get dc.
   */
  protected function getDc() {
    if (!isset($this['DC'])) {
      $dc_datastream = $this->constructDatastream('DC', 'M');
      $dc_datastream->mimetype = 'application/xml';
      $dc_datastream->label = 'DC Record';
      $mods_content = $this->getMods();
      if ($mods_content) {
        $dc_datastream->content = static::runXslTransform(
          array(
            'xsl' => $this->modsToDcTransform,
            'input' => $mods_content,
          )
        );
        $this->ingestDatastream($dc_datastream);
      }
      else {
        return FALSE;
      }
    }
    return $this['DC']->content;
  }
}

class RochdoraBatchPageObject extends IslandoraScanBatchObject {
  protected $parentId;
  protected $sequenceNumber;
  protected $volumeBase;
  protected $pageBase;

  /**
   * Constructor.
   */
  public function __construct(IslandoraTuque $connection, $parent_id, $sequence, $object_info, $pp, $volume_base) {
    parent::__construct($connection, $parent_id, $object_info, $pp);
    $this->parentId = $parent_id;
    $this->sequenceNumber = $sequence;
    $this->volumeBase = $volume_base;
    $this->label = "$parent_id-$sequence";
    // Munge out what page is for matching underneath.
    $this->pageBase = str_replace('_p3', '', $object_info->name);
  }

  /**
   * Function getChildren.
   */
  public function getChildren(IslandoraTuque $connection) {
    // Go and get occlusions.
    $children = array();

    $target = $this->preprocessorParameters['target'] . '/Phase3/' . $this->volumeBase;
    $mask = "/($this->pageBase)(.*)_p3z\.tif/";
    $all_occlusions = rochdora_retrieve_files_for_object($target, $mask);

    // Filter out the single occlusion objects.
    $page_base = $this->pageBase;
    $filter = function ($occlusion) use ($page_base) {
      return preg_match("/{$page_base}_p3z\.tif/", $occlusion->filename);
    };
    $single_occlusions = array_filter($all_occlusions, $filter);

    // Filter out the rest of the objects.
    $filter = function ($occlusion) use ($page_base, $single_occlusions) {
      preg_match("/{$page_base}[a-z]_p3z\.tif/", $occlusion->filename, $match);
      // Have to handle a file naming case where a single occlusion has a verso
      // and thus would match the file naming scheme with a 'v' on the end. Ex:
      // prefix_095_p3z.tif and prefix_095v_p3z.tif.
      if (!empty($match)) {
        $potential_key = "{$page_base}_p3z.tif";
        return !isset($single_occlusions[$potential_key]);
      }
      return FALSE;
    };
    $multiple_occlusions = array_filter($all_occlusions, $filter);

    $occlusions = array_merge($single_occlusions, $multiple_occlusions);
    ksort($occlusions);
    $sequence_number = 1;
    foreach ($occlusions as $occlusion) {
      $occlusion_base = str_replace('_p3z', '', $occlusion->name);
      $children[] = new RochdoraBatchOcclusionObject($connection, $this->id, $occlusion, $sequence_number, $this->preprocessorParameters, $this->volumeBase, $occlusion_base, $this->label);
      $sequence_number = $sequence_number + 1;
    }

    // Go find all the occluded images to create as well.
    $target_tiff = "{$this->preprocessorParameters['target']}/Phase1/{$this->volumeBase}/TIFFs/{$this->pageBase}.tif";
    if (file_exists($target_tiff)) {
      $file = new stdClass();
      $file->uri = $target_tiff;
      $file->filename = "{$this->pageBase}.tif";
      $file->name = pathinfo("{$this->pageBase}.tif", PATHINFO_FILENAME);
      $children[] = new RochdoraBatchOccludedImage($connection, $this->id, $file, $this->preprocessorParameters, $this->volumeBase, $this->pageBase);
    }
    return $children;
  }

  /**
   * Add collection and content model relationships.
   */
  public function addRelationships() {
    module_load_include('inc', 'islandora_paged_content', 'includes/utilities');
    // Add relationship to collection.
    $rels_ext = $this->relationships;
    islandora_paged_content_set_relationship($rels_ext, ISLANDORA_RELS_EXT_URI, 'isPageOf', $this->parentId);
    islandora_paged_content_set_relationship($rels_ext, ISLANDORA_RELS_EXT_URI, 'isSequenceNumber', (string) $this->sequenceNumber, TRUE);
    islandora_paged_content_set_relationship($rels_ext, ISLANDORA_RELS_EXT_URI, 'isPageNumber', (string) $this->sequenceNumber, TRUE);
    islandora_paged_content_set_relationship($rels_ext, ISLANDORA_RELS_EXT_URI, 'isSection', '1', TRUE);
    islandora_paged_content_set_relationship($rels_ext, FEDORA_RELS_EXT_URI, 'isMemberOf', $this->parentId);
    $this->addContentModelRelationships();
    module_load_include('inc', 'islandora_ocr', 'includes/derivatives');
    islandora_ocr_set_generate_ocr_rels_ext_statement($this, TRUE);
  }

  /**
   * Add the content model relationship(s).
   */
  protected function addContentModelRelationships() {
    $this->models = array('islandora:pageCModel');
  }

  /**
   * Function batch_process.
   */
  public function batchProcess() {
    $mods = $this->getMods();
    if ($mods) {
      unset($this->label);
      $this->label = $this->getTitle();
    }
    $this->getDc();
    $this->getTei();
    $this->getObj();
    return ISLANDORA_BATCH_STATE__DONE;
  }

  /**
   * Function to get the mods.
   */
  protected function getMods() {
    if (!isset($this['MODS'])) {
      module_load_include('inc', 'rochdora', 'includes/utilities');
      $mods_datastream = $this->constructDatastream('MODS', 'M');
      $mods_datastream->mimetype = 'application/xml';
      $mods_datastream->label = 'MODS Record';

      // Go and attempt to retrieve the MODS for the volume.
      $target_mods = "{$this->preprocessorParameters['target']}/Phase5/metadata/{$this->volumeBase}/{$this->pageBase}_p3.mods";
      if (file_exists($target_mods)) {
        $mods_datastream->setContentFromFile($target_mods, FALSE);
      }
      else {
        return FALSE;
      }
      $this->ingestDatastream($mods_datastream);
    }
    return $this['MODS']->content;
  }
  /**
   * Function to get dc.
   */
  protected function getDc() {
    if (!isset($this['DC'])) {
      $dc_datastream = $this->constructDatastream('DC', 'M');
      $dc_datastream->mimetype = 'application/xml';
      $dc_datastream->label = 'DC Record';
      $mods_content = $this->getMods();
      if ($mods_content) {
        $dc_datastream->content = static::runXslTransform(
          array(
            'xsl' => $this->modsToDcTransform,
            'input' => $mods_content,
          )
        );
        $this->ingestDatastream($dc_datastream);
      }
      else {
        return FALSE;
      }
    }
    return $this['DC']->content;
  }

  /**
   * Function to get the TEI.
   */
  protected function getTei() {
    if (!isset($this['TEI'])) {
      module_load_include('inc', 'rochdora', 'includes/utilities');
      $tei_datastream = $this->constructDatastream('TEI', 'M');
      $tei_datastream->mimetype = 'application/xml';
      $tei_datastream->label = 'TEI Record';

      // Go and attempt to retrieve the TEI for the volume.
      $target_tei = "{$this->preprocessorParameters['target']}/Phase5/final/{$this->volumeBase}/{$this->pageBase}_p3.tei";
      if (file_exists($target_tei)) {
        $tei_datastream->setContentFromFile($target_tei, FALSE);
      }
      else {
        return FALSE;
      }
      $this->ingestDatastream($tei_datastream);
    }
    return $this['TEI']->content;
  }

  /**
   * Function to get the OBJ.
   */
  protected function getObj() {
    if (!isset($this['OBJ'])) {
      $obj_datastream = $this->constructDatastream('OBJ', 'M');
      $obj_datastream->mimetype = 'image/tiff';
      $obj_datastream->label = 'OBJ Datastream';
      $obj_datastream->setContentFromFile($this->objectInfo->uri, FALSE);
      $this->ingestDatastream($obj_datastream);
    }
  }
}

class RochdoraBatchOccludedImage extends IslandoraScanBatchObject {
  protected $parentId;
  protected $volumeBase;
  protected $pageBase;

  /**
   * Constructor.
   */
  public function __construct(IslandoraTuque $connection, $parent_id, $object_info, $pp, $volume_base, $page_base) {
    $label = $object_info->name;
    parent::__construct($connection, $label, $object_info, $pp);
    $this->parentId = $parent_id;
    $this->volumeBase = $volume_base;
    $this->pageBase = $page_base;
  }

  /**
   * Function getChildren.
   */
  public function getChildren(IslandoraTuque $connection) {
    return array();
  }

  /**
   * Add collection and content model relationships.
   */
  public function addRelationships() {
    $this->relationships->add(FEDORA_RELS_EXT_URI, 'isPartOf', $this->parentId);
    $this->addContentModelRelationships();
  }

  /**
   * Add the content model relationship(s).
   */
  protected function addContentModelRelationships() {
    $this->models = array('islandora:sp_large_image_cmodel');
  }

  /**
   * Function batch_process.
   */
  public function batchProcess() {
    $this->getObj();
    $this->getTitle();

    if (!isset($this->label)) {
      $this->label = t('!parent_id Occluded Image', array('!parent_id' => $this->parentId));
    }
    else {
      $this->label = t('Occluded Image of !label', array('!label' => $this->label));
    }
    return ISLANDORA_BATCH_STATE__DONE;
  }

  /**
   * Function to get the OBJ.
   */
  protected function getObj() {
    if (!isset($this['OBJ'])) {
      $obj_datastream = $this->constructDatastream('OBJ', 'M');
      $obj_datastream->mimetype = 'image/tiff';
      $obj_datastream->label = 'OBJ Datastream';
      $obj_datastream->setContentFromFile($this->objectInfo->uri, FALSE);
      $this->ingestDatastream($obj_datastream);
    }
  }

  /**
   * Spoof function to get the MODS to set a title.
   */
  protected function getMods() {
    // Since the parent object isn't going to be ingested we need to do a bit of
    // shenanigans to get its label.
    $target_mods = "{$this->preprocessorParameters['target']}/Phase5/metadata/{$this->volumeBase}/{$this->pageBase}_p3.mods";
    if (file_exists($target_mods)) {
      return file_get_contents($target_mods);
    }
    else {
      return FALSE;
    }
  }
}

class RochdoraBatchOcclusionObject extends IslandoraScanBatchObject {
  protected $parentId;
  protected $volumeBase;
  protected $occlusionBase;
  protected $sequenceNumber;

  /**
   * Constructor.
   */
  public function __construct(IslandoraTuque $connection, $parent_id, $object_info, $sequence_number, $pp, $volume_base, $occlusion_base, $parent_label) {
    $label = format_string('!parent_label - Occlusion !sequence', array('!parent_label' => $parent_label, '!sequence' => $sequence_number));
    parent::__construct($connection, $label, $object_info, $pp);
    $this->parentId = $parent_id;
    $this->volumeBase = $volume_base;
    $this->occlusionBase = $occlusion_base;
    $this->sequenceNumber = $sequence_number;
    $this->label = $label;
  }

  /**
   * Function getChildren.
   */
  public function getChildren(IslandoraTuque $connection) {
    // Go and get occlusion images (front and back).
    $children = array();
    $target = $this->preprocessorParameters['target'] . '/Phase3/' . $this->volumeBase;
    $mask = "/$this->occlusionBase(.*)_p3z\.tif/";
    $occlusion_images = rochdora_retrieve_files_for_object($target, $mask);
    $sequence_number = 1;
    foreach ($occlusion_images as $image) {
      $children[] = new RochdoraBatchOcclusionImage($connection, $this->id, $image, $sequence_number, $this->preprocessorParameters, $this->volumeBase, $this->occlusionBase);
      $sequence_number = $sequence_number + 1;
    }
    return $children;
  }

  /**
   * Add collection and content model relationships.
   */
  public function addRelationships() {
    $rels_predicate = variable_get('islandora_compound_object_relationship', 'isConstituentOf');
    $this->relationships->add(FEDORA_RELS_EXT_URI, $rels_predicate, $this->parentId);
    $escaped_pid = str_replace(':', '_', $this->parentId);
    $this->relationships->add(ISLANDORA_RELS_EXT_URI, "isSequenceNumberOf$escaped_pid", $this->sequenceNumber, RELS_TYPE_PLAIN_LITERAL);
    $this->addContentModelRelationships();
  }

  /**
   * Add the content model relationship(s).
   */
  protected function addContentModelRelationships() {
    $this->models = array('islandora:compoundCModel');
  }

  /**
   * Function batch_process.
   */
  public function batchProcess() {
    return ISLANDORA_BATCH_STATE__DONE;
  }
}

class RochdoraBatchOcclusionImage extends IslandoraScanBatchObject {
  protected $parentId;
  protected $volumeBase;
  protected $occlusionBase;
  protected $sequenceNumber;

  /**
   * Constructor.
   */
  public function __construct(IslandoraTuque $connection, $parent_id, $object_info, $sequence_number, $pp, $volume_base, $occlusion_base) {
    // Sniff out from the file name if this a front or back and name as such.
    if (preg_match("/{$occlusion_base}_[0-9]{3}_p3z/", $object_info->name)) {
      $label = t('Front');
    }
    else {
      $label = t('Back');
    }
    parent::__construct($connection, $label, $object_info, $pp);
    $this->parentId = $parent_id;
    $this->volumeBase = $volume_base;
    $this->occlusionBase = $occlusion_base;
    $this->sequenceNumber = $sequence_number;
    $this->label = $label;
  }

  /**
   * Function getChildren.
   */
  public function getChildren(IslandoraTuque $connection) {
    return array();
  }

  /**
   * Add collection and content model relationships.
   */
  public function addRelationships() {
    $rels_predicate = variable_get('islandora_compound_object_relationship', 'isConstituentOf');
    $this->relationships->add(FEDORA_RELS_EXT_URI, $rels_predicate, $this->parentId);
    $escaped_pid = str_replace(':', '_', $this->parentId);
    $this->relationships->add(ISLANDORA_RELS_EXT_URI, "isSequenceNumberOf$escaped_pid", $this->sequenceNumber, RELS_TYPE_PLAIN_LITERAL);
    $this->addContentModelRelationships();
  }

  /**
   * Add the content model relationship(s).
   */
  protected function addContentModelRelationships() {
    $this->models = array('islandora:sp_large_image_cmodel');
  }

  /**
   * Function batch_process.
   */
  public function batchProcess() {
    $this->getObj();
    return ISLANDORA_BATCH_STATE__DONE;
  }

  /**
   * Function to get the OBJ.
   */
  protected function getObj() {
    if (!isset($this['OBJ'])) {
      $obj_datastream = $this->constructDatastream('OBJ', 'M');
      $obj_datastream->mimetype = 'image/tiff';
      $obj_datastream->label = 'OBJ Datastream';
      $obj_datastream->setContentFromFile($this->objectInfo->uri, FALSE);
      $this->ingestDatastream($obj_datastream);
    }
  }
}
