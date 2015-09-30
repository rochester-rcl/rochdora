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
    $mask = "/Phase3\/TIFFs\/{$this->baseName}\/(.*)_p3\.tif/";
    $pages = rochdora_retrieve_files_for_object($this->preprocessorParameters['file_listing'], $mask);
    $sequence_number = 1;
    foreach ($pages as $page) {
      $children[] = new RochdoraBatchPageObject($connection, $this->id, $sequence_number, $page, $this->preprocessorParameters, $this->baseName);
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
    if (in_array('islandora:bookCModel', $this->models) && $this->preprocessorParameters['parent'] == 'islandora:diaries') {
      $this->label = str_replace('Diaries', 'Diary', $this->label);
    }
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
  public function __construct(IslandoraTuque $connection, $parent_id, $sequence, $object_info, $pp, $volume_base) {
    parent::__construct($connection, $parent_id, $object_info, $pp);
    $this->parentId = $parent_id;
    $this->sequenceNumber = $sequence;
    $this->volumeBase = $volume_base;
    $this->label = "$parent_id-$sequence";
    // Munge out what page is for matching underneath.
    $this->pageBase = str_replace('_p3', '', $object_info->name);
    $this->volumeBaseLower = strtolower($this->volumeBase);
    $this->pageBaseLower = strtolower($this->pageBase);
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

    // Go find all the occluded images to create as well.
    $target_tiff = "Phase1/{$this->volumeBase}/TIFFs/{$this->pageBase}.tif";
    if (isset($this->preprocessorParameters['file_listing'][$target_tiff])) {
      $file = new stdClass();
      $file->uri = $this->preprocessorParameters['file_listing'][$target_tiff]->uri;
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
      $children[] = new RochdoraBatchOcclusionImage($connection, $this->id, $image, $sequence_number, $this->preprocessorParameters, $this->volumeBase, $this->pageBase, $this->occlusionBase);
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
   * Spoof function to get the MODS to set a title.
   */
  protected function getMods() {
    // Since the parent object isn't going to be ingested we need to do a bit of
    // shenanigans to get its label.
    $target_mods = "Phase5/metadata/mods_inc_{$this->volumeBaseLower}/{$this->pageBaseLower}_p3.mods";
    if (isset($this->preprocessorParameters['file_listing'][$target_mods])) {
      return file_get_contents($this->preprocessorParameters['file_listing'][$target_mods]->uri);
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
    if (!isset($this->label) || empty($this->label)) {
      // First check and see if we can munge from the TEI, if not attempt to
      // fall back on MODS.
      $target_tei = "Phase5/tei/{$this->volumeBaseLower}/{$this->pageBaseLower}_p3.tei";
      if (isset($this->preprocessorParameters['file_listing'][$target_tei])) {
        // Go sniff out a base label for this image.
        $tei = file_get_contents($this->preprocessorParameters['file_listing'][$target_tei]->uri);
        // Strip off xml-model so libxml doesn't have a fit.
        $tei = preg_replace('/<\?xml-model .*\?>/', '', $tei);
        $dom = new DOMDocument();
        $dom->loadXML($tei);
        $xpath = new DOMXPath($dom);
        $xpath->registerNamespace('tei', 'http://www.tei-c.org/ns/1.0');
        $label_xpath = format_string('normalize-space(/tei:TEI/tei:teiHeader/tei:fileDesc/tei:sourceDesc/tei:msDesc/tei:physDesc/tei:additions/tei:figure[tei:graphic[@url="!front_filename" or @url="!back_filename"]])', array(
          '!front_filename' => "{$this->occlusionBase}_p3.jpg",
          '!back_filename' => "{$this->occlusionBase}_p3z.jpg",
        ));
        $base_label = $xpath->evaluate($label_xpath);
        if (!empty($base_label)) {
          $this->label = $base_label;
        }
      }
      if (empty($base_label)) {
        $mods = $this->getMods();
        if ($mods) {
          $mods_doc = new DOMDocument();
          $mods_doc->loadXML($mods);
          $mods_xpath = new DOMXPath($mods_doc);
          $mods_xpath->registerNamespace('m', 'http://www.loc.gov/mods/v3');

          $this->label = $mods_xpath->evaluate('string(//m:mods/m:titleInfo/m:title/text())');
        }
        $this->label = format_string('!parent_label - Occlusion !sequence', array('!parent_label' => isset($this->label) ? $this->label : $this->parentId, '!sequence' => $this->sequenceNumber));
      }
      else {
        $this->label = $base_label;
      }
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
  public function __construct(IslandoraTuque $connection, $parent_id, $object_info, $sequence_number, $pp, $volume_base, $page_base, $occlusion_base) {
    // Sniff out from the file name if this a front or back and name as such.
    $target_tei = 'Phase5/tei/' . strtolower($volume_base) . '/' . strtolower($occlusion_base) . '_p3.tei';
    if (isset($pp['file_listing'][$target_tei])) {
      // Go sniff out a base label for this image.
      $tei = file_get_contents($pp['file_listing'][$target_tei]->uri);
      // Strip off xml-model so libxml doesn't have a fit.
      $tei = preg_replace('/<\?xml-model .*\?>/', '', $tei);
      $dom = new DOMDocument();
      $dom->loadXML($tei);
      $xpath = new DOMXPath($dom);
      $xpath->registerNamespace('tei', 'http://www.tei-c.org/ns/1.0');
      $label_xpath = format_string('normalize-space(/tei:TEI/tei:teiHeader/tei:fileDesc/tei:sourceDesc/tei:msDesc/tei:physDesc/tei:additions/tei:figure[tei:graphic[@url="!front_filename" or @url="!back_filename"]])', array(
        '!front_filename' => "{$occlusion_base}_p3.jpg",
        '!back_filename' => "{$occlusion_base}_p3z.jpg",
      ));
      $base_label = $xpath->evaluate($label_xpath);
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
