# -*- coding: utf-8 -*-
#
# Copyright 2016 Capital One Services, LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

from ._tpt import Export

from .constants import *
from .errors import *

from .config import Config
from .connection import Connection
from .encoders import dict_to_json
from .fmt import truncate
from .logging import log
from .sql import parse_statement, remove_curly_quotes
from .utils import get_version_info, show_warning, suppress_context

from ._compat import *


__all__ = ['TeradataExport']


class TeradataExport(Connection):
    """
    The class for using the Teradata Parallel Transport API to quickly eport
    large amounts of data.

    Exposed under the alias :class:`giraffez.Export`.

    :param str query: Either SQL query to execute and return results of, **or** the
        name of a table to export in its entirety
    :param str host: Omit to read from :code:`~/.girafferc` configuration file.
    :param str username: Omit to read from :code:`~/.girafferc` configuration file.
    :param str password: Omit to read from :code:`~/.girafferc` configuration file.
    :param str delimiter: The delimiter to use when exporting with the 'text'
        encoding. Defaults to '|'
    :param str null: The string to use to represent a null value with the
        'text' encoding. Defaults to 'NULL'
    :param str encoding: The encoding format in which to export the data.
        Defaults to 'text'.  Possible values are 'text' - delimited text 
        output, 'dict' - to output each row as a :code:`dict` object mapping column names
        to values, 'json' - to output each row as a JSON encoded string, and 
        'archive' to output in giraffez archive format
    :param int log_level: Specify the desired level of output from the job.
        Possible values are :code:`giraffez.SILENCE` :code:`giraffez.INFO` (default),
        :code:`giraffez.VERBOSE` and :code:`giraffez.DEBUG`
    :param str config: Specify an alternate configuration file to be read from, when previous paramters are omitted.
    :param str key_file: Specify an alternate key file to use for configuration decryption
    :param string dsn: Specify a connection name from the configuration file to be
        used, in place of the default.
    :param bool protect: If authentication with Teradata fails and :code:`protect` is :code:`True` 
        locks the connection used in the configuration file. This can be unlocked using the
        command :code:`giraffez config --unlock <connection>` changing the connection password,
        or via the :meth:`~giraffez.config.Config.unlock_connection` method.
    :raises `giraffez.errors.InvalidCredentialsError`: if the supplied credentials are incorrect
    :raises `giraffez.errors.TeradataError`: if the connection cannot be established

    Meant to be used, where possible, with Python's :code:`with` context handler
    to guarantee that connections will be closed gracefully when operation
    is complete:

    .. code-block:: python

       with giraffez.Export('dbc.dbcinfo') as export:
           print(export.header)
           for row in export.results():
               print(row)
    """

    def __init__(self, query=None, host=None, username=None, password=None,
            log_level=INFO, config=None, key_file=None, dsn=None, protect=False,
            delimiter="|", null=None, coerce_floats=True):
        super(TeradataExport, self).__init__(host, username, password, log_level, config, key_file,
            dsn, protect)

        #: Attributes used with property getter/setters
        self._query = None
        self._encoding = None
        self._delimiter = delimiter
        self._null = null

        self.coerce_floats = coerce_floats

        self.initiated = False

        #: Attributes set via property getter/setters
        self.query = query

    def _connect(self, host, username, password):
        self.export = Export(host, username, password)
        self.export.add_attribute(TD_QUERY_BAND_SESS_INFO, "UTILITYNAME={};VERSION={};".format(
            *get_version_info()))
        self.delimiter = DEFAULT_DELIMITER
        self.null = DEFAULT_NULL

    def close(self):
        log.info("Export", "Closing Teradata PT connection ...")
        self.export.close()
        log.info("Export", "Teradata PT request complete.")

    @property
    def columns(self):
        """
        Retrieve columns if necessary, and return them.

        :rtype: :class:`~giraffez.types.Columns`
        """
        return self.export.columns()

    @property
    def delimiter(self):
        return self._delimiter

    @delimiter.setter
    def delimiter(self, value):
        self._delimiter = value

    @property
    def null(self):
        return self._null

    @null.setter
    def null(self, value):
        self._null = value
        self.export.set_null(self._null)
    
    @property
    def header(self):
        """
        Return the header for the resulting data.

        :return: String containing the formatted header, either column names
            separated by the current :code:`delimiter` value if :code:`encoding` is 'text',
            or the serialized :class:`~giraffez.types.Columns` object for
            'archive' encoding
        :rtype: str
        :raises `giraffez.errors.GiraffeError`: if the query or table
            has not been specified
        """
        if self.query is None:
            raise GiraffeError("Must set target table or query.")
        return self.delimiter.join(self.columns.names)

    def initiate(self):
        log.info("Export", "Initiating Teradata PT request (awaiting server)  ...")
        self.export.initiate()
        self.initiated = True
        log.info("Export", "Teradata PT request accepted.")
        self.options("delimiter", escape_string(self.delimiter), 2)
        self.options("null", self.null, 3)
        log.info("Export", "Executing ...")
        log.info(self.options)

    @property
    def query(self):
        """
        :return: The current query if it has been set, otherwise :code:`None`
        :rtype: str
        """
        return self._query

    @query.setter
    def query(self, query):
        """
        Set the query to be run and initiate the connection with Teradata.
        Only necessary if the query/table name was not specified as an argument
        to the constructor of the instance.

        :param str query: Valid SQL query to be executed
        """
        if query is None:
            return
        if log.level >= VERBOSE:
            self.options("query", query, 5)
        else:
            self.options("query", truncate(query), 5)
        statements = parse_statement(remove_curly_quotes(query))
        if not statements:
            raise GiraffeError("Unable to parse SQL statement")
        if len(statements) > 1:
            show_warning(("MORE THAN ONE STATEMENT RECEIVED, EXPORT OPERATIONS ALLOW ONE "
                "STATEMENT - ONLY THE FIRST STATEMENT WILL BE USED."), RuntimeWarning)
        statement = statements[0]
        log.debug("Debug[2]", "Statement (sanitized): {!r}".format(statement))
        if not (statement.startswith("select ") or statement.startswith("sel ")):
            statement = "select * from {}".format(statement)
        if statement == self.query:
            return
        else:
            self._query = statement
        self.initiated = False
        self.export.set_query(statement)

    def archive(self, writer):
        if 'b' not in writer.mode:
            raise GiraffeError("Archive writer must be in binary mode")
        self.export.set_encoding(ROW_ENCODING_RAW)
        writer.write(GIRAFFE_MAGIC)
        writer.write(self.columns.serialize())
        for line in self.fetchall():
            writer.write(line)

    def json(self):
        self.processor = dict_to_json
        return self.items()

    def items(self):
        self.processor = identity
        self.export.set_encoding(ROW_ENCODING_DICT)
        if self.coerce_floats:
            self.export.set_encoding(DECIMAL_AS_FLOAT)
        return self.fetchall()

    def strings(self):
        self.processor = identity
        self.export.set_encoding(ROW_ENCODING_STRING|DATETIME_AS_STRING|DECIMAL_AS_STRING)
        return self.fetchall()

    def values(self):
        self.processor = identity
        self.export.set_encoding(ROW_ENCODING_LIST)
        if self.coerce_floats:
            self.export.set_encoding(DECIMAL_AS_FLOAT)
        return self.fetchall()

    def fetchall(self):
        """
        Generates and yields row results as they are returned from
        Teradata and processed. 
        
        :return: A generator that can be iterated over or coerced into a list.
        :rtype: iterator (yields ``string`` or ``dict``)

        .. code-block:: python

           # iterate over results
           with giraffez.Export('dbc.dbcinfo') as export:
               for row in export.results():
                   print(row)

           # retrieve results as a list
           with giraffez.Export('db.table2') as export:
               results = list(export.results())
        """
        if self.query is None:
            raise GiraffeError("Must set target table or query.")
        if not self.initiated:
            # TODO: this may not be necessary, InvalidCredentialsError
            # is being checked in the base class.
            try:
                self.initiate()
            except InvalidCredentialsError as error:
                if self.protect:
                    Config.lock_connection(self.config, self.dsn)
                raise suppress_context(error)
        while True:
            data = self.export.get_buffer()
            if not data:
                break
            for row in data:
                yield self.processor(row)
