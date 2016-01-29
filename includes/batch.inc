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
   * Perform preprocessing of the scanned resources.
   */
  public function preprocess() {
    $files = $this->scan();
    $this->parameters['file_listing'] = $files;

    $added = array();

    $grouped = $this->groupFiles($files);

    $object_class = static::getObjectClass();
    foreach ($grouped as $base_name => $object_info) {
      $ingest_object = new $object_class($this->connection, $base_name, $object_info, $this->parameters);
      $added = array_merge($added, $this->preprocessChildren($ingest_object));
    }

    return $added;
  }

  /**
   * Group our files into base volumes to start out.
   */
  protected function groupFiles($files) {
    // Filter out down to our base directory.
    $grouped = array();
    foreach ($files as $uri => &$file_object) {
      // Looking only for the base Phase 1 directories to start as our initial
      // volume object.
      if (strpos($uri, 'Phase1') !== FALSE) {
        $path_parts = explode(DIRECTORY_SEPARATOR, $uri);
        // Want all directories that dictate a volume object.
        $key = $path_parts[1];
        if (!isset($grouped[$key])) {
          $grouped[$key] = $key;
        }
      }
    }
    return $grouped;
  }
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
   */
  public function getChildren(IslandoraTuque $connection) {
    module_load_include('inc', 'rochdora', 'includes/utilities');
    $children = array();
    // There is a disconnect in quality of the scans done in Phase1 versus those
    // done in Phase3. When an image has no inclusions we want to use the Phase1
    // image as the page as it is a higher quality scan. Grab both sets of
    // images here now. Operating under the assumption that if a MODS file is
    // present for an object that there is an occluded image (an image with the
    // source page and all clippings present) and an unoccluded image.
    $phase3_mask = "/Phase3\/TIFFs\/{$this->baseName}\/(.*)_p3\.tif/";
    $phase3_images = rochdora_retrieve_files_for_object($this->preprocessorParameters['file_listing'], $phase3_mask);
    $phase1_mask = "/Phase1\/{$this->baseName}\/TIFFs\/(.*)\.tif/";
    $phase1_images = rochdora_retrieve_files_for_object($this->preprocessorParameters['file_listing'], $phase1_mask);
    // Grab the MODS inclusion for reference down the chain.
    $mods_mask = "/Phase5\/metadata\/mods_inc_{$this->baseName}\/(.*)\.mods/";
    $mods_inc = rochdora_retrieve_files_for_object($this->preprocessorParameters['file_listing'], $mods_mask);
    $pages = array();
    $occluded_images = array();
    foreach ($phase3_images as $key => $values) {
      // Let's see if we have a matching inclusion.
      $inclusion_mods = "{$values->name}.mods";
      $potential_mods_key = "Phase5/metadata/mods_inc_{$this->baseName}/{$inclusion_mods}";
      // Occluded images have a bit different file naming, so let's munge it
      // now.
      $occluded_key = str_replace('_p3', '', $values->name);
      $potential_occluded_key = "Phase1/{$this->baseName}/TIFFs/{$occluded_key}.tif";
      // There's an inclusion MODS record present, need to use the Phase3 image,
      // similary need to construct an occluded image!
      if (isset($mods_inc[$potential_mods_key])) {
        $pages[$key] = $values;
        if (isset($phase1_images[$potential_occluded_key])) {
          // Tie the key to the page image so we can grab it below!
          $occluded_images[$values->uri] = $phase1_images[$potential_occluded_key];
        }
      }
      else {
        // Use the Phase1 image if it exists, otherwise default back to Phase3.
        // Data inconsistencies yay, also don't create an occluded image.
        if (isset($phase1_images[$potential_occluded_key])) {
          $pages[$potential_occluded_key] = $phase1_images[$potential_occluded_key];
        }
        else {
          $pages[$key] = $values;
        }
      }
    }
    $sequence_number = 1;
    foreach ($pages as $page) {
      $occluded_image = isset($occluded_images[$page->uri]) ? $occluded_images[$page->uri] : FALSE;
      $children[] = new RochdoraBatchPageObject($connection, $this->id, $sequence_number, $page, $this->preprocessorParameters, $this->baseName, $occluded_image);
      $sequence_number = $sequence_number + 1;
    }
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
    $this->getTitle();
    $this->getMods();
    $this->getDc();
    return ISLANDORA_BATCH_STATE__DONE;
  }

  /**
   * Gets the title for this object.
   */
  protected function getTitle() {
    parent::getTitle();
    $this->label = str_replace('Diaries', 'Diary', $this->label);
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
      $mask = "/Phase5\/metadata\/mods_{$this->baseName}.mods/";
      $mods_files = rochdora_retrieve_files_for_object($this->preprocessorParameters['file_listing'], $mask);
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
  public function __construct(IslandoraTuque $connection, $parent_id, $sequence, $object_info, $pp, $volume_base, $occluded_image) {
    parent::__construct($connection, $parent_id, $object_info, $pp);
    $this->parentId = $parent_id;
    $this->sequenceNumber = $sequence;
    $this->volumeBase = $volume_base;
    $this->label = "$parent_id-$sequence";
    // Munge out what page is for matching underneath.
    $this->pageBase = str_replace('_p3', '', $object_info->name);
    $this->volumeBaseLower = strtolower($this->volumeBase);
    $this->pageBaseLower = strtolower($this->pageBase);
    $this->occludedImage = $occluded_image;
  }

  /**
   * Function getChildren.
   */
  public function getChildren(IslandoraTuque $connection) {
    // Go and get occlusions.
    $children = array();

    $mask = "/Phase3\/TIFFs\/{$this->volumeBase}\/($this->pageBase)(.*)_p3z\.tif/";
    $all_occlusions = rochdora_retrieve_files_for_object($this->preprocessorParameters['file_listing'], $mask);

    // Filter out the single occlusion objects.
    $page_base = $this->pageBase;
    $volume_base = $this->volumeBase;
    $filter = function ($occlusion) use ($page_base) {
      return preg_match("/{$page_base}_p3z\.tif/", $occlusion->filename);
    };
    $single_occlusions = array_filter($all_occlusions, $filter);

    // Filter out the rest of the objects.
    $filter = function ($occlusion) use ($page_base, $single_occlusions, $volume_base) {
      preg_match("/{$page_base}[a-z]_p3z\.tif/", $occlusion->filename, $match);
      // Have to handle a file naming case where a single occlusion has a verso
      // and thus would match the file naming scheme with a 'v' on the end. Ex:
      // prefix_095_p3z.tif and prefix_095v_p3z.tif.
      if (!empty($match)) {
        $potential_key = "Phase3/TIFFs/{$volume_base}/{$page_base}_p3z.tif";
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
      $children[] = new RochdoraBatchOcclusionObject($connection, $this->id, $occlusion, $sequence_number, $this->preprocessorParameters, $this->volumeBase, $this->pageBase, $occlusion_base);
      $sequence_number = $sequence_number + 1;
    }

    // If there's an occluded image for this page add it too.
    if ($this->occludedImage) {
      $children[] = new RochdoraBatchOccludedImage($connection, $this->id, $this->occludedImage, $this->preprocessorParameters, $this->volumeBase, $this->pageBase);
    }
    return $children;
  }

  /**
   * Add collection and content model relationships.
   */
  public function addRelationships() {
    module_load_include('inc', 'islandora_paged_content', 'includes/utilities');
    islandora_paged_content_add_relationships_to_child($this, array(
      'model' => 'islandora:pageCModel',
      'parent' => $this->parentId,
      'page_number' => $this->sequenceNumber,
      'language' => 'eng',
      'extract_text' => 'tesseract',
    ));
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

      // Go and attempt to retrieve the MODS for the page.
      $target_mods = "Phase5/metadata/mods_{$this->volumeBaseLower}/{$this->pageBaseLower}_p3.mods";
      if (isset($this->preprocessorParameters['file_listing'][$target_mods])) {
        $mods_datastream->setContentFromFile($this->preprocessorParameters['file_listing'][$target_mods]->uri, FALSE);
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

      // Go and attempt to retrieve the TEI for the page.
      $target_tei = "Phase5/tei/{$this->volumeBaseLower}/{$this->pageBaseLower}_p3.tei";
      if (isset($this->preprocessorParameters['file_listing'][$target_tei])) {
        $tei_datastream->setContentFromFile($this->preprocessorParameters['file_listing'][$target_tei]->uri, FALSE);
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
    parent::__construct($connection, $object_info->name, $object_info, $pp);
    $this->parentId = $parent_id;
    $this->volumeBase = $volume_base;
    $this->pageBase = $page_base;
    $this->volumeBaseLower = strtolower($this->volumeBase);
    $this->pageBaseLower = strtolower($this->pageBase);
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
    $target_mods = "Phase5/metadata/mods_{$this->volumeBaseLower}/{$this->pageBaseLower}_p3.mods";
    if (isset($this->preprocessorParameters['file_listing'][$target_mods])) {
      return file_get_contents($this->preprocessorParameters['file_listing'][$target_mods]->uri);
    }
    else {
      return FALSE;
    }
  }
}

class RochdoraBatchOcclusionObject extends IslandoraScanBatchObject {
  protected $parentId;
  protected $volumeBase;
  protected $pageBase;
  protected $occlusionBase;
  protected $sequenceNumber;

  /**
   * Constructor.
   */
  public function __construct(IslandoraTuque $connection, $parent_id, $object_info, $sequence_number, $pp, $volume_base, $page_base, $occlusion_base) {
    parent::__construct($connection, $object_info->name, $object_info, $pp);
    $this->parentId = $parent_id;
    $this->volumeBase = $volume_base;
    $this->occlusionBase = $occlusion_base;
    $this->pageBase = $page_base;
    $this->sequenceNumber = $sequence_number;
    $this->volumeBaseLower = strtolower($this->volumeBase);
    $this->pageBaseLower = strtolower($this->pageBase);
  }

  /**
   * Function getChildren.
   */
  public function getChildren(IslandoraTuque $connection) {
    // Go and get occlusion images (front and back).
    $children = array();
    $mask = "/Phase3\/TIFFs\/{$this->volumeBase}\/{$this->occlusionBase}(v)?_p3z\.tif/";
    $occlusion_images = rochdora_retrieve_files_for_object($this->preprocessorParameters['file_listing'], $mask);
    $sequence_number = 1;
    foreach ($occlusion_images as $image) {
      $children[] = new RochdoraBatchOcclusionImage($connection, $this->id, $image, $sequence_number, $this->preprocessorParameters, $this->volumeBase, $this->pageBase, $this->occlusionBase, $this->objectInfo->name);
      $sequence_number = $sequence_number + 1;
    }
    return $children;
  }

  /**
   * Add collection and content model relationships.
   */
  public function addRelationships() {
    // Can't use the compound helper function as it expects the object to be
    // be present to determine how to set the sequence number.
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
    $this->getTitle();
    return ISLANDORA_BATCH_STATE__DONE;
  }

  /**
   * Function to get the MODS.
   */
  protected function getMods() {
    $target_mods = "Phase5/metadata/mods_inc_{$this->volumeBaseLower}/{$this->pageBaseLower}_p3.mods";
    if (isset($this->preprocessorParameters['file_listing'][$target_mods])) {
      if (!isset($this['MODS'])) {
        module_load_include('inc', 'rochdora', 'includes/utilities');
        $dom = new DOMDocument('1.0', 'UTF-8');
        $dom->load($this->preprocessorParameters['file_listing'][$target_mods]->uri);
        $xpath = new DOMXPath($dom);
        $xpath->registerNamespace('mods', 'http://loc.gov/v3/mods');
        // The identifiers in MODS are stored with a .jpg extension as opposed
        // to tif, weird right?
        $mods_result = $xpath->evaluate(format_string('/mods:modsCollection/mods:mods[mods:identifier[@type="fileName"] = "!filename"]', array(
          '!filename' => "{$this->objectInfo->name}.jpg",
        )));
        if ($mods_result->length > 0) {
          $mods_item = $mods_result->item(0);
          // Need to make a new DOM so we can get the parent namespaces.
          $new_dom = new DOMDocument('1.0', 'UTF-8');
          $node = $new_dom->importNode($mods_item, TRUE);
          $new_dom->appendChild($node);
          $mods_datastream = $this->constructDatastream('MODS', 'M');
          $mods_datastream->mimetype = 'application/xml';
          $mods_datastream->label = 'MODS Record';
          $mods_datastream->content = $new_dom->saveXML();
          $this->ingestDatastream($mods_datastream);
        }
        else {
          return FALSE;
        }
      }
      return $this['MODS']->content;
    }
    else {
      return FALSE;
    }
  }

  /**
   * Get the title for this object.
   *
   * Gets the label not empty; otherwise, grabs a title from the MODS.
   */
  protected function getTitle() {
    parent::getTitle();
    if (!isset($this->label) || empty($this->label)) {
      $this->label = format_string('!parent_label - Occlusion !sequence', array('!parent_label' => isset($this->label) ? $this->label : $this->parentId, '!sequence' => $this->sequenceNumber));
    }
    return $this->label;
  }
}

class RochdoraBatchOcclusionImage extends IslandoraScanBatchObject {
  protected $parentId;
  protected $volumeBase;
  protected $pageBase;
  protected $occlusionBase;
  protected $sequenceNumber;

  /**
   * Constructor.
   */
  public function __construct(IslandoraTuque $connection, $parent_id, $object_info, $sequence_number, $pp, $volume_base, $page_base, $occlusion_base, $parent_filename) {
    // Sniff out from the file name if this a front or back and name as such.
    $target_mods = format_string('Phase5/metadata/mods_inc_!volume_base/!page_base_p3.mods', array(
      '!volume_base' => strtolower($volume_base),
      '!page_base' => strtolower($page_base),
    ));
    if (isset($pp['file_listing'][$target_mods])) {
      // Similarly like the parent these are stored within a MODS collection
      // wrapper. Using the abstract here for the title until told otherwise.
      $dom = new DOMDocument('1.0', 'UTF-8');
      $dom->load($pp['file_listing'][$target_mods]->uri);
      $xpath = new DOMXPath($dom);
      $xpath->registerNamespace('mods', 'http://loc.gov/v3/mods');
      // The identifiers in MODS are stored with a .jpg extension as opposed
      // to tif, weird right?
      $base_label = $xpath->evaluate(format_string('string(/mods:modsCollectison/mods:mods[mods:identifier[@type="fileName"] = "!filename"]/mods:abstract)', array(
        '!filename' => "{$parent_filename}.jpg",
      )));
    }
    if (preg_match("/{$occlusion_base}_p3z/", $object_info->name)) {
      if (!empty($base_label)) {
        $label = t('!base_label - Front', array('!base_label' => $base_label));
      }
      else {
        $label = t('Front');
      }
    }
    else {
      if (!empty($base_label)) {
        $label = t('!base_label - Back', array('!base_label' => $base_label));
      }
      else {
        $label = t('Back');
      }
    }
    parent::__construct($connection, $label, $object_info, $pp);
    $this->parentId = $parent_id;
    $this->volumeBase = $volume_base;
    $this->occlusionBase = $occlusion_base;
    $this->sequenceNumber = $sequence_number;
    $this->label = $label;
    $this->pageBase = $page_base;
  }

  /**
   * Function getChildren.
   */
  public function getChildren(IslandoraTuque $connection) {
    return array();
  }

  /**
   * Add compound and content model relationships.
   */
  public function addRelationships() {
    // Can't use the compound helper function as it expects the object to be
    // be present to determine how to set the sequence number.
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

/**
 * Gets the batch for cleaning thumbnail versions.
 */
function rochdora_get_thumbnail_version_cleaning_batch() {
  return array(
    'operations' => array(
      array('rochdora_remove_extra_thumbnail_versions', array()),
    ),
    'title' => t('Cleaning thumbnail versions ...'),
    'init_message' => t('Preparing to clean thumbnail versions.'),
    'progress_message' => t('Time elapsed: @elapsed <br/>Estimated time remaining: @estimate.'),
    'error_message' => t('An error has occurred.'),
    'file' => drupal_get_path('module', 'rochdora') . '/includes/batch.inc',
  );
}

/**
 * Operation for cleaning thumbnail versions.
 *
 * @param array $context
 *   The batch context.
 */
function rochdora_remove_extra_thumbnail_versions(&$context) {
  $connection = islandora_get_tuque_connection();
  $query = <<<EOQ
SELECT ?pid
FROM <#ri>
WHERE {
  ?pid <fedora-view:disseminates> ?tn .
  ?tn <fedora-view:disseminationType> <info:fedora/*/TN> .
  ?pid <fedora-view:disseminates> ?obj .
  ?obj <fedora-view:disseminationType> <info:fedora/*/OBJ> .
}
EOQ;

  // Base info for the first run.
  $sandbox = &$context['sandbox'];
  if (!isset($sandbox['offset'])) {
    $sandbox['offset'] = 0;
    $sandbox['total'] = $connection->repository->ri->countQuery($query, 'sparql');
    if ($sandbox['total'] === 0) {
      return;
    }
  }

  $context['message'] = dt('Processing results @start-@end out of @total ...', array(
    '@start' => $sandbox['offset'],
    '@end' => min($sandbox['offset'] + 10, $sandbox['total']),
    '@total' => $sandbox['total'],
  ));

  // Add to the query for getting the objects.
  $query .= <<<EOQ
LIMIT 10
OFFSET {$sandbox['offset']}
EOQ;

  // Fix the actual objects.
  $results = $connection->repository->ri->sparqlQuery($query);
  foreach ($results as $result) {
    $object = islandora_object_load($result['pid']['value']);
    $tns = count($object['TN']);
    $objs = count($object['OBJ']);
    if ($tns > $objs) {
      $earliest = $object['TN'][0];
      $properties = array(
        'content' => $earliest->content,
        'label' => $earliest->label,
        'state' => $earliest->state,
        'mimetype' => $earliest->mimetype,
      );
      $object->purgeDatastream('TN');
      $new_tn = $object->constructDatastream('TN', 'M');
      foreach ($properties as $property => $value) {
        $new_tn->{$property} = $value;
      }
      $new_tn->setContentFromString($earliest_version);
      $object->ingestDatastream($new_tn);
      $context['message'] = dt('@pid: reset thumbnail to earliest version.', array(
        '@pid' => $object->id,
      ));
    }
  }

  $sandbox['offset'] += 10;
  $context['finished'] = $sandbox['offset'] / $sandbox['total'];
}
